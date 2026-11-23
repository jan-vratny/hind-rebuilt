#include "render/gauges.hpp"

#include <algorithm>
#include <utility>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

#include "gamedata/original.hpp"
#include "render/font.hpp"
#include "render/shaders.hpp"

namespace render {

namespace {

constexpr float kPi = 3.14159265f;
// Colours of the original's cockpit palette (COCKPITS/COCKPITS.RGB).
const glm::vec3 kNeedle = glm::vec3(159.0f) / 255.0f;
const glm::vec3 kBright = glm::vec3(207.0f) / 255.0f;
const glm::vec3 kAmber = glm::vec3(171, 91, 0) / 255.0f;
const glm::vec3 kSky = glm::vec3(83, 143, 171) / 255.0f;
const glm::vec3 kGround = glm::vec3(11, 71, 103) / 255.0f;
const glm::vec3 kBallTop = glm::vec3(175.0f) / 255.0f;
const glm::vec3 kBallBottom = glm::vec3(71.0f) / 255.0f;
const glm::vec3 kRed{0.9f, 0.15f, 0.1f};

// Dial centres in the pilot's picture, measured on the dial faces of COCKPITS/*.640 found in it.
const glm::vec2 kGMeter{122.0f, 288.0f}, kClock{480.0f, 300.0f}, kAirspeed{204.0f, 356.0f}, kVsi{396.5f, 356.5f},
    kRadar{116.0f, 444.0f}, kBaro{204.0f, 436.0f}, kHsi{300.5f, 434.5f};
const glm::vec2 kPyr{85.0f, 325.0f};  // top left corner of the pitch/yaw/roll dial (SAIBACK.640)
// Windows of the picture: main and standby attitude indicators, moving map.
const glm::vec2 kAdi{300.0f, 344.5f}, kStandby{396.0f, 436.0f};
const glm::vec2 kMap0{464.0f, 344.0f}, kMap1{632.0f, 464.0f};
const float kMapMetresPerPixel = 40.0f;  // picture pixels: 6.7 km across
// The gunner's four dials on his left console (GBARO, GVSI, GAIR, GRALT.640).
const glm::vec2 kGunBaro{-478.0f, 423.0f}, kGunVsi{-399.0f, 423.0f}, kGunAirspeed{-320.0f, 423.0f},
    kGunRadar{-240.0f, 423.0f};
// Lamps in the pilot's view to the left: brakes, the three gear legs (nose, left, right).
const glm::vec2 kBrakeLamp{-36.0f, 460.0f};
const glm::vec2 kGearLamps[3] = {{-43.0f, 444.0f}, {-29.0f, 429.0f}, {-15.0f, 429.0f}};

// Angle clockwise from 12 o'clock to a direction in picture pixels (y down).
glm::vec2 dir(float a) { return {std::sin(a), -std::cos(a)}; }
float deg(float d) { return d * kPi / 180.0f; }

std::vector<glm::vec2> circle(glm::vec2 c, float r, int segs = 32) {
  std::vector<glm::vec2> p;
  for (int i = 0; i < segs; ++i) p.push_back(c + r * dir(2.0f * kPi * i / segs));
  return p;
}

// Piecewise linear scale: value -> dial angle in degrees (or pixels), points in increasing order.
float curve(float v, std::initializer_list<glm::vec2> points) {
  const glm::vec2* p = points.begin();
  if (v <= p[0].x) return p[0].y;
  for (size_t i = 1; i < points.size(); ++i)
    if (v <= p[i].x) return glm::mix(p[i - 1].y, p[i].y, (v - p[i - 1].x) / (p[i].x - p[i - 1].x));
  return p[points.size() - 1].y;
}

const char* kMapFS = R"(#version 330 core
uniform vec4 uRect;      // screen x0, y0, x1, y1 (y up)
uniform vec2 uCenter;    // world x, z at the rectangle's centre
uniform float uMpp;      // metres per screen pixel
uniform vec2 uOrigin;
uniform float uSize;
uniform sampler2D uMap;
uniform vec3 uOutside;   // beyond the picture: the land's average colour
uniform float uLight;    // 1 by day; at night the land goes dark, the water less so
out vec4 fragColor;
void main() {
  vec2 f = gl_FragCoord.xy;
  if (f.x < uRect.x || f.x > uRect.z || f.y < uRect.y || f.y > uRect.w) discard;
  vec2 d = (f - 0.5 * (uRect.xy + uRect.zw)) * uMpp;
  vec2 world = vec2(uCenter.x + d.x, uCenter.y - d.y);  // north (-z) up
  vec2 uv = (world - uOrigin) / uSize;
  vec3 col = texture(uMap, uv).rgb;
  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) col = uOutside;
  bool water = col.b > col.g * 1.1 && col.b > col.r * 1.3;
  fragColor = vec4(col * (water ? max(uLight, 0.5) : uLight), 1.0);
}
)";

}  // namespace

PanelGauges::~PanelGauges() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (mapVao_) glDeleteVertexArrays(1, &mapVao_);
  if (mapTex_) glDeleteTextures(1, &mapTex_);
}

bool PanelGauges::init(const world::Terrain& terrain) {
  if (!prog_.build("gauges", shaders::kHudVS, shaders::kHudFS)) return false;
  if (!mapProg_.build("map", shaders::kSkyVS, kMapFS)) return false;
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, p));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, c));
  glGenVertexArrays(1, &mapVao_);
  buildMap(terrain, terrain.airfield().center);
  return true;
}

void PanelGauges::buildMap(const world::Terrain& terrain, glm::vec2 centre) {
  // Map picture: the colours of a war zone with contour lines every 200 m, or for the procedural
  // landscape ochre land shaded by height, contours every 50 m, blue water, darker forest; and
  // the runway and apron.
  const int n = 1024;
  const world::Airfield& af = terrain.airfield();
  mapSize_ = 26000.0f;
  mapOrigin_ = centre - glm::vec2(mapSize_ * 0.5f);
  mapCentre_ = glm::vec2(1e9f);
  const float step = mapSize_ / n;
  std::vector<float> height(size_t(n) * n);
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      height[size_t(j) * n + i] = terrain.rawHeight(mapOrigin_.x + (i + 0.5f) * step, mapOrigin_.y + (j + 0.5f) * step);
    }
  }
  std::vector<unsigned char> px(size_t(n) * n * 3);
  glm::dvec3 sum(0.0);
  for (int j = 0; j < n; ++j) {
    for (int i = 0; i < n; ++i) {
      float hgt = height[size_t(j) * n + i];
      float x = mapOrigin_.x + (i + 0.5f) * step, z = mapOrigin_.y + (j + 0.5f) * step;
      glm::vec3 c;
      const float interval = terrain.mapped() ? 200.0f : 50.0f;
      if (terrain.mapped()) {
        c = terrain.mapColour(x, z);
        int band = int(std::floor(hgt / interval));
        bool contour = (i + 1 < n && int(std::floor(height[size_t(j) * n + i + 1] / interval)) != band) ||
                       (j + 1 < n && int(std::floor(height[size_t(j + 1) * n + i] / interval)) != band);
        if (contour) c *= 0.80f;
      } else if (hgt < 0.05f) {
        c = {0.20f, 0.36f, 0.62f};
      } else {
        float t = std::clamp(hgt / 700.0f, 0.0f, 1.0f);
        c = glm::mix(glm::vec3(0.58f, 0.46f, 0.26f), glm::vec3(0.80f, 0.68f, 0.46f), t);
        if (terrain.forestDensity(x, z) > 0.5f) c *= 0.86f;
        int band = int(std::floor(hgt / 50.0f));
        bool contour = (i + 1 < n && int(std::floor(height[size_t(j) * n + i + 1] / 50.0f)) != band) ||
                       (j + 1 < n && int(std::floor(height[size_t(j + 1) * n + i] / 50.0f)) != band);
        if (contour) c *= 0.80f;
      }
      if (!af.runways.empty()) {
        for (const world::Runway& r : af.runways) {
          const glm::vec2 d = r.b - r.a, p = glm::vec2(x, z) - r.a;
          const glm::vec2 q = p - d * std::clamp(glm::dot(p, d) / glm::dot(d, d), 0.0f, 1.0f);
          if (glm::length(q) < std::max(r.width * 0.5f, 40.0f)) c = {0.22f, 0.22f, 0.22f};
        }
      } else {
        glm::vec2 a = glm::vec2(x, z) - af.center;
        if (std::fabs(a.x) < 40.0f && std::fabs(a.y) < af.runwayLength * 0.5f) c = {0.22f, 0.22f, 0.22f};
        glm::vec2 ap = glm::vec2(x, z) - (af.center + af.apronCenter);
        if (std::fabs(ap.x) < af.apronHalf.x && std::fabs(ap.y) < af.apronHalf.y) c = {0.38f, 0.38f, 0.36f};
      }
      unsigned char* p = &px[(size_t(j) * n + i) * 3];
      for (int k = 0; k < 3; ++k) p[k] = (unsigned char)(std::clamp(c[k], 0.0f, 1.0f) * 255.0f);
      sum += glm::dvec3(glm::clamp(c, 0.0f, 1.0f));
    }
  }
  mapOutside_ = glm::vec3(sum / double(n * n));
  if (!mapTex_) glGenTextures(1, &mapTex_);
  glBindTexture(GL_TEXTURE_2D, mapTex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, n, n, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

namespace {

std::vector<uint8_t> readOriginal(const std::string& dir, const char* file) {
  std::ifstream f(gamedata::findOriginalFile(dir, file), std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

void PanelGauges::loadParts(const std::string& originalDir, const gamedata::PaletteRange& palette) {
  struct Part {
    const char* file;
    int width;
    gamedata::Picture* to;
  };
  for (const Part& l : {Part{"COCKPITS/BRAKEON.640", 20, &brakeOn_}, Part{"COCKPITS/UCLDOWN.640", 6, &gearDown_},
                        Part{"COCKPITS/UCLUP.640", 6, &gearMoving_}, Part{"COCKPITS/WARNING.640", 20, &warning_},
                        Part{"COCKPITS/TWION.640", 20, &twiOn_}, Part{"COCKPITS/AWARNON.640", 8, &awarnOn_},
                        Part{"COCKPITS/LITE_ON.640", 5, &liteOn_}, Part{"COCKPITS/NUMERICS.640", 6, &numerics_},
                        Part{"COCKPITS/WEAP_NOS.640", 6, &weapDigits_}, Part{"COCKPITS/WEAP_TXT.640", 6, &weapFont_},
                        Part{"COCKPITS/HMAPBAR.640", 168, &mapBarH_}, Part{"COCKPITS/VMAPBAR.640", 11, &mapBarV_},
                        Part{"COCKPITS/CMAPBAR.640", 15, &mapBarC_}, Part{"COCKPITS/ENDMISS.640", 248, &endMission_}}) {
    std::string error, path = gamedata::findOriginalFile(originalDir, l.file);
    if (path.empty() || !gamedata::loadRaw(path, l.width, palette, *l.to, error)) *l.to = {};
  }
  // The weapon font is drawn over its window: only the ink, not the background (colour 234).
  if (weapFont_.width && palette.first <= 234 && palette.rgb.size() >= size_t(234 - palette.first + 1) * 3) {
    const uint8_t* bg = &palette.rgb[size_t(234 - palette.first) * 3];
    for (size_t i = 0; i + 3 < weapFont_.rgba.size(); i += 4)
      if (std::equal(bg, bg + 3, &weapFont_.rgba[i])) weapFont_.rgba[i + 3] = 0;
  }

  // SMALLNUM.640: 5 pixels wide, digits 0..9 of 3 x 5 pixels every 6 rows from row 7, dark ink
  // (colours 230, 232) with softened corners (227).
  digits_.clear();
  std::vector<uint8_t> d = readOriginal(originalDir, "COCKPITS/SMALLNUM.640");
  if (d.size() == 5 * 79)
    for (int g = 0; g < 10; ++g)
      for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 3; ++x) {
          uint8_t c = d[size_t(7 + 6 * g + y) * 5 + 1 + x];
          digits_.push_back(c == 230 || c == 232 ? 1.0f : c == 227 ? 0.5f : 0.0f);
        }

  // RWRBLAT.640 (79 x 75), the threat warning panel as painted in the picture: its lamps are
  // brown (colours 246, 247) and light up orange (248, 249). Cut it into the direction lamps, the
  // classification lamps 1..6 and the ring of proximity segments around the aircraft symbol.
  rwrRing_.clear();
  for (LitPart& p : rwrDir_) p = {};
  for (LitPart& p : rwrClass_) p = {};
  d = readOriginal(originalDir, "COCKPITS/RWRBLAT.640");
  const int W = 79, H = 75;
  if (d.size() != size_t(W * H) || palette.first > 246 || palette.rgb.size() < size_t(250 - palette.first) * 3) return;
  auto colour = [&](int idx) {
    const uint8_t* c = &palette.rgb[size_t(idx - palette.first) * 3];
    return glm::vec3(c[0], c[1], c[2]) / 255.0f;
  };
  auto lamp = [&](int x, int y) { return d[size_t(y * W + x)] == 246 || d[size_t(y * W + x)] == 247; };
  auto take = [&](LitPart& part, int x0, int y0, int x1, int y1, std::vector<bool>& used) {
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x)
        if (lamp(x, y) && !used[size_t(y * W + x)]) {
          used[size_t(y * W + x)] = true;
          part.px.push_back({x, y});
          part.col.push_back(colour(d[size_t(y * W + x)] + 2));
        }
  };
  std::vector<bool> used(size_t(W * H), false);
  const int dirs[10][4] = {{28, 0, 38, 8},  {44, 0, 54, 8},  {16, 5, 26, 13},  {56, 5, 66, 13},  {8, 16, 18, 24},
                           {64, 16, 74, 24}, {6, 29, 16, 37}, {66, 29, 76, 37}, {14, 53, 27, 61}, {51, 53, 65, 61}};
  for (int i = 0; i < 10; ++i) take(rwrDir_[i], dirs[i][0], dirs[i][1], dirs[i][2], dirs[i][3], used);
  for (int i = 0; i < 6; ++i) take(rwrClass_[i], 7 + 11 * i, 67, std::min(17 + 11 * i, W - 1), 73, used);
  // What is left near the aircraft symbol (but not the lock circle under it) falls apart into
  // the ring's segments; they light clockwise from the lower left.
  std::vector<bool> inner(size_t(W * H), false);
  std::vector<uint8_t> lock = readOriginal(originalDir, "COCKPITS/RWRLOCK.640");
  if (lock.size() == 33 * 32)
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 33; ++x) inner[size_t((17 + y) * W + 25 + x)] = lock[size_t(y * 33 + x)] >= 246;
  std::vector<std::pair<float, LitPart>> ring;
  const glm::vec2 centre{39.5f, 32.0f};
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      if (!lamp(x, y) || used[size_t(y * W + x)] || inner[size_t(y * W + x)]) continue;
      LitPart part;
      std::vector<glm::ivec2> stack{{x, y}};
      used[size_t(y * W + x)] = true;
      while (!stack.empty()) {
        glm::ivec2 p = stack.back();
        stack.pop_back();
        part.px.push_back(glm::vec2(p));
        part.col.push_back(colour(d[size_t(p.y * W + p.x)] + 2));
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            glm::ivec2 q = p + glm::ivec2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= W || q.y >= H || used[size_t(q.y * W + q.x)] || !lamp(q.x, q.y) ||
                inner[size_t(q.y * W + q.x)])
              continue;
            used[size_t(q.y * W + q.x)] = true;
            stack.push_back(q);
          }
      }
      glm::vec2 c{0.0f};
      for (glm::vec2 p : part.px) c += p;
      c = c / float(part.px.size()) - centre;
      if (part.px.size() < 8 || glm::length(c) < 15.0f || glm::length(c) > 24.0f) continue;
      float angle = std::fmod(glm::degrees(std::atan2(c.x, -c.y)) + 360.0f - 200.0f, 360.0f);
      ring.push_back({angle, std::move(part)});
    }
  std::sort(ring.begin(), ring.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (auto& r : ring) rwrRing_.push_back(std::move(r.second));
}

// ---- drawing primitives (picture pixels)

void PanelGauges::tri(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec3 col) {
  glm::vec4 c4(col, alpha_);
  for (glm::vec2 p : {a, b, c}) verts_.push_back({at(p), c4});
}

void PanelGauges::quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, glm::vec3 col) {
  tri(a, b, c, col);
  tri(a, c, d, col);
}

void PanelGauges::rect(glm::vec2 a, glm::vec2 b, glm::vec3 col) { quad(a, {b.x, a.y}, b, {a.x, b.y}, col); }

void PanelGauges::poly(const std::vector<glm::vec2>& pts, glm::vec3 col) {
  for (size_t i = 1; i + 1 < pts.size(); ++i) tri(pts[0], pts[i], pts[i + 1], col);
}

void PanelGauges::disc(glm::vec2 c, float r, glm::vec3 col, int segs) { poly(circle(c, r, segs), col); }

void PanelGauges::segment(glm::vec2 a, glm::vec2 b, float width, glm::vec3 col) {
  glm::vec2 d = b - a;
  float len = glm::length(d);
  if (len < 1e-5f) return;
  glm::vec2 s = glm::vec2(-d.y, d.x) / len * (width * 0.5f);
  quad(a - s, b - s, b + s, a + s, col);
}

// A needle of the original: a thin line from the hub.
void PanelGauges::needle(glm::vec2 c, float angle, float len) { segment(c, c + len * dir(angle), 1.1f, kNeedle); }

void PanelGauges::text(const std::string& s, glm::vec2 centre, glm::vec3 col) {
  // Pixel font in whole screen pixels, centred on the point.
  float unit = std::max(1.0f, std::floor(scale_ + 0.3f));
  float width = (float(s.size()) * (kGlyphW + 1) - 1) * unit, height = kGlyphH * unit;
  glm::vec2 o = glm::floor(at(centre) - glm::vec2(width, height) * 0.5f);
  glm::vec4 c4(col, 1.0f);
  for (char ch : s) {
    const GlyphRows& rows = glyphRows(ch);
    for (int r = 0; r < kGlyphH; ++r) {
      for (int k = 0; k < kGlyphW; ++k) {
        if (!(rows[size_t(r)] >> (kGlyphW - 1 - k) & 1)) continue;
        glm::vec2 p = o + glm::vec2(k, kGlyphH - 1 - r) * unit;
        for (glm::vec2 q : {p, p + glm::vec2(unit, 0), p + glm::vec2(unit), p, p + glm::vec2(unit), p + glm::vec2(0, unit)})
          verts_.push_back({q, c4});
      }
    }
    o.x += (kGlyphW + 1) * unit;
  }
}

void PanelGauges::sprite(const gamedata::Picture& p, glm::vec2 topLeft, int row0, int rows) {
  if (rows < 0) rows = p.height - row0;
  for (int y = std::max(row0, 0); y < std::min(row0 + rows, p.height); ++y)
    for (int x = 0; x < p.width; ++x) {
      const uint8_t* c = &p.rgba[(size_t(y) * p.width + x) * 4];
      glm::vec2 at = topLeft + glm::vec2(x, y - row0);
      if (c[3]) rect(at, at + glm::vec2(1.0f), glm::vec3(c[0], c[1], c[2]) / 255.0f);
    }
}

void PanelGauges::drum(float value, int digits, glm::vec2 topLeft, float pitch) {
  // NUMERICS.640: wheels 9 pixels a digit, 0..9 and 0 again, seen through a window 9 rows high.
  if (!numerics_.width) return;
  value = std::max(value, 0.0f);
  float scale = 1.0f;
  for (int k = 1; k < digits; ++k) scale *= 10.0f;
  for (int k = 0; k < digits; ++k, scale /= 10.0f) {
    float wheel = std::fmod(value / scale, 10.0f);
    // Only the last wheel turns smoothly; the others click over as it passes 9.
    if (k < digits - 1) {
      float below = std::fmod(value, scale);
      wheel = std::floor(wheel) + std::max(0.0f, below - (scale - 1.0f));
    }
    sprite(numerics_, topLeft + glm::vec2(pitch * float(k), 0.0f), int(std::lround(wheel * 9.0f)), 9);
  }
}

void PanelGauges::lit(const LitPart& part, glm::vec2 origin) {
  for (size_t i = 0; i < part.px.size(); ++i) rect(origin + part.px[i], origin + part.px[i] + glm::vec2(1.0f), part.col[i]);
}

void PanelGauges::smallText(const std::string& s, glm::vec2 centre, glm::vec3 col) {
  if (digits_.empty()) return text(s, centre, col);
  glm::vec2 o = glm::floor(centre - glm::vec2(4.0f * float(s.size()) - 1.0f, 5.0f) * 0.5f + 0.5f);
  for (char ch : s) {
    if (ch >= '0' && ch <= '9')
      for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 3; ++x) {
          float a = digits_[size_t(ch - '0') * 15 + size_t(y) * 3 + size_t(x)];
          if (a <= 0.0f) continue;
          alpha_ = a;
          rect(o + glm::vec2(x, y), o + glm::vec2(x + 1, y + 1), col);
        }
    o.x += 4.0f;
  }
  alpha_ = 1.0f;
}

// ---- dials, calibrated on the scales painted on the original's faces (angles clockwise from 12)

void PanelGauges::gMeter(const InstrumentData& d, glm::vec2 c, float r) {
  needle(c, deg(curve(d.gLoad, {{-2, -102}, {-1, -46}, {0, 0}, {1, 46}, {2, 102}, {3, 149}, {4, 208}})), r);
}

void PanelGauges::clock(const InstrumentData& d, glm::vec2 c, float r) {
  float s = d.clockSeconds;
  needle(c, 2.0f * kPi * std::fmod(s / 43200.0f, 1.0f), r * 0.6f);
  needle(c, 2.0f * kPi * std::fmod(s / 3600.0f, 1.0f), r);
}

void PanelGauges::airspeed(const InstrumentData& d, glm::vec2 c, float r) {
  // Tens of km/h, 0 to 45 over 324 degrees.
  needle(c, deg(std::clamp(d.speedKmh, 0.0f, 470.0f) * 0.72f), r);
}

void PanelGauges::verticalSpeed(const InstrumentData& d, glm::vec2 c, float r) {
  // VAR-30MK: metres per minute / 10, zero at 9 o'clock, climb clockwise over the top, finer up to 10.
  float v = d.verticalSpeed * 6.0f;
  needle(c, deg(curve(v, {{-30, -270}, {-20, -225}, {-10, -180}, {0, -90}, {10, 0}, {20, 45}, {30, 90}})), r);
}

void PanelGauges::radarAltimeter(const InstrumentData& d, glm::vec2 c, float r) {
  // RV-5: 0 to 100 m on the first half of the dial, up to 700 m on the rest; parked below zero when off.
  float m = d.radarAltitude < 0.0f ? -30.0f : d.radarAltitude;
  needle(c, deg(curve(m, {{-30, -20}, {0, 0}, {20, 27}, {40, 57}, {60, 89}, {80, 123}, {100, 158}, {300, 202},
                          {500, 251}, {700, 313}})), r);
}

void PanelGauges::baroAltimeter(const InstrumentData& d, glm::vec2 c, float r) {
  // Long needle: hundreds of metres; short one: thousands.
  float m = std::max(d.baroAltitude, 0.0f);
  needle(c, 2.0f * kPi * m / 10000.0f, r * 0.6f);
  needle(c, 2.0f * kPi * m / 1000.0f, r);
}

void PanelGauges::hsi(const InstrumentData& d) {
  // RMI-2: the compass card turns, heading at 12 o'clock, marked 0, 3, 6 ... 33 (tens of degrees);
  // the pointer shows the way home or to the next waypoint. Heading counter in the left window
  // under the dial.
  const float r = 25.5f;
  float h = deg(d.headingDeg);
  for (int i = 0; i < 36; ++i) {
    float a = deg(10.0f * i) - h;
    segment(kHsi + r * dir(a), kHsi + r * (i % 3 ? 0.88f : 0.8f) * dir(a), 1.0f, kNeedle);
    if (i % 3 == 0) smallText(std::to_string(i), kHsi + r * 0.62f * dir(a), kNeedle);
  }
  glm::vec2 home = d.steer - d.position;
  if (glm::length(home) > 50.0f) needle(kHsi, std::atan2(home.x, -home.y) - h, r * 0.9f);
  // Counters under the dial: heading, and the distance there in km with its tenths apart.
  if (numerics_.width) {
    drum(std::fmod(d.headingDeg + 360.0f, 360.0f), 3, {275.0f, 470.0f});
    float km = std::min(glm::length(home) / 1000.0f, 99.9f);
    drum(std::floor(km), 2, {302.0f, 470.0f});
    drum(std::fmod(km * 10.0f, 10.0f), 1, {321.0f, 470.0f});
  } else {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%03d", int(std::lround(d.headingDeg)) % 360);
    text(buf, {285.0f, 474.0f}, kBright);
  }
}

void PanelGauges::pitchYawRoll(const InstrumentData& d) {
  // Rocket aiming indicator: pitch marker up the vertical scale, yaw (sideslip) marker along the
  // horizontal one, a roll bar along the curved scale on the left (10 degrees each way).
  const glm::vec2 o = kPyr;
  float py = o.y + curve(d.pitchDeg, {{-20, 52}, {0, 38.5f}, {20, 20}, {40, 8}});
  rect({o.x + 34, py - 1}, {o.x + 36, py + 1}, kAmber);
  float sx = o.x + curve(d.slipDeg, {{-20, 20}, {0, 36.5f}, {20, 50.5f}});
  rect({sx - 0.5f, o.y + 36}, {sx + 0.5f, o.y + 38}, kAmber);
  float ry = 31.0f - std::clamp(d.rollDeg, -10.0f, 10.0f) * 2.1f, rx = 10.0f + 13.0f * std::pow((ry - 31.0f) / 22.0f, 2.0f);
  rect({o.x + rx - 6, o.y + ry - 0.5f}, {o.x + rx - 1, o.y + ry + 0.5f}, kNeedle);
}

void PanelGauges::ballFace(glm::vec2 centre, float r, glm::vec2 h0, glm::vec2 up, glm::vec3 above, glm::vec3 below) {
  // Only the window: the picture is drawn over it, but its windscreen is transparent and a face
  // larger than the window would show through it when pitched far.
  const std::vector<glm::vec2> disc = circle(centre, r, 48);
  for (float side : {1.0f, -1.0f}) {
    std::vector<glm::vec2> half;
    for (size_t i = 0; i < disc.size(); ++i) {
      const glm::vec2 a = disc[i], b = disc[(i + 1) % disc.size()];
      const float da = side * glm::dot(a - h0, up), db = side * glm::dot(b - h0, up);
      if (da >= 0.0f) half.push_back(a);
      if ((da >= 0.0f) != (db >= 0.0f)) half.push_back(a + (b - a) * (da / (da - db)));
    }
    if (half.size() >= 3) poly(half, side > 0.0f ? above : below);
  }
}

void PanelGauges::horizon(const InstrumentData& d) {
  // Main attitude indicator through the round window: sky and ground rolling and pitching,
  // with pitch lines every 10 degrees. The picture's bezel crops it.
  float roll = deg(d.rollDeg);
  glm::vec2 up{std::sin(roll), -std::cos(roll)}, along{std::cos(roll), std::sin(roll)};
  const float ppd = 1.35f;  // picture pixels per degree of pitch
  glm::vec2 h0 = kAdi + up * (-std::clamp(d.pitchDeg, -60.0f, 60.0f) * ppd);
  ballFace(kAdi, 32.0f, h0, up, kSky, kGround);
  for (int p = -60; p <= 60; p += 10) {
    if (p == 0) continue;
    glm::vec2 m = h0 + up * (float(p) * ppd);
    if (glm::length(m - kAdi) < 32.0f) segment(m - along * 15.0f, m + along * 15.0f, 1.0f, kBright);
  }
}

void PanelGauges::standbyHorizon(const InstrumentData& d) {
  // A ball, light above the horizon and dark below, with a pitch scale every 5 degrees.
  float roll = deg(d.rollDeg);
  glm::vec2 up{std::sin(roll), -std::cos(roll)}, along{std::cos(roll), std::sin(roll)};
  const float ppd = 0.6f;
  glm::vec2 h0 = kStandby + up * (-std::clamp(d.pitchDeg, -80.0f, 80.0f) * ppd);
  ballFace(kStandby, 24.0f, h0, up, kBallTop, kBallBottom);
  for (int p = -80; p <= 80; p += 5) {
    glm::vec2 m = h0 + up * (float(p) * ppd);
    float half = p % 10 ? 1.5f : 3.0f;
    if (glm::length(m - kStandby) < 22.0f) segment(m - along * half, m + along * half, 0.8f, p > 0 ? kBallBottom : kBallTop);
  }
}

void PanelGauges::map(const InstrumentData& d) {
  // As in the original: the map does not turn or scroll. The position is where the cursor bars
  // cross, and when they come near the edge the map recentres on it. The airfield is ringed.
  const glm::vec2 c = (kMap0 + kMap1) * 0.5f;
  glm::vec2 p = glm::floor(c + (d.position - mapCentre_) / kMapMetresPerPixel + 0.5f);
  auto home = [&] { return c + (d.home - mapCentre_) / kMapMetresPerPixel; };
  // A mission's route: legs as thin dark lines, the waypoints as small squares, the next one larger.
  auto onMap = [&](glm::vec2 w) { return c + (w - mapCentre_) / kMapMetresPerPixel; };
  for (size_t i = 0; i < d.route.size(); ++i) {
    glm::vec2 a = onMap(d.route[i]);
    if (i + 1 < d.route.size()) {
      glm::vec2 b = onMap(d.route[i + 1]);
      // Clipped to the map: beyond it would show through the other windows of the panel.
      float t0 = 0.0f, t1 = 1.0f;
      glm::vec2 dd = b - a;
      bool inside = true;
      for (int k = 0; k < 2 && inside; ++k)
        for (float edge : {kMap0[k], kMap1[k]}) {
          bool low = edge == kMap0[k];
          float num = low ? a[k] - edge : edge - a[k], den = low ? -dd[k] : dd[k];
          if (std::fabs(den) < 1e-6f) {
            if (num < 0.0f) inside = false;
            continue;
          }
          float t = num / den;
          if (den < 0.0f) t0 = std::max(t0, t);
          else t1 = std::min(t1, t);
        }
      if (inside && t0 < t1) segment(a + dd * t0, a + dd * t1, 1.0f, glm::vec3(0.12f));
    }
    const float s = int(i) == d.waypoint ? 3.0f : 2.0f;
    if (a.x > kMap0.x + s && a.x < kMap1.x - s && a.y > kMap0.y + s && a.y < kMap1.y - s)
      rect(a - glm::vec2(s), a + glm::vec2(s), int(i) == d.waypoint ? glm::vec3(0.75f, 0.1f, 0.05f) : glm::vec3(0.12f));
  }
  glm::vec2 h = home();
  if (h.x > kMap0.x - 6 && h.x < kMap1.x + 6 && h.y > kMap0.y - 6 && h.y < kMap1.y + 6) {
    auto ring = circle(h, 6.0f, 16);
    for (size_t i = 0; i < ring.size(); ++i) segment(ring[i], ring[(i + 1) % ring.size()], 1.0f, glm::vec3(0.1f));
  }
  // HMAPBAR.640 and VMAPBAR.640: pairs of rails 11 pixels apart across the whole map,
  // CMAPBAR.640: their crossing with an X on the spot.
  if (mapBarH_.width && mapBarV_.width && mapBarC_.width) {
    sprite(mapBarH_, {kMap0.x, p.y - 5.0f});
    sprite(mapBarV_, {p.x - 5.0f, kMap0.y});
    sprite(mapBarC_, p - glm::vec2(7.0f));
  } else {
    segment({kMap0.x, p.y + 0.5f}, {kMap1.x, p.y + 0.5f}, 1.0f, glm::vec3(0.15f));
    segment({p.x + 0.5f, kMap0.y}, {p.x + 0.5f, kMap1.y}, 1.0f, glm::vec3(0.15f));
  }
}

void PanelGauges::lamps(const InstrumentData& d) {
  if (d.brakes && brakeOn_.width) sprite(brakeOn_, kBrakeLamp);
  const gamedata::Picture& gear = d.gearMoving ? gearMoving_ : gearDown_;
  if ((d.gearDown || d.gearMoving) && gear.width)
    for (glm::vec2 p : kGearLamps) sprite(gear, p);
  // The failure warning lights as the manual lists them (item 18): the panel on the left and
  // the block by the attitude indicator. WARNING.640 has 18 lamps of 20 x 12 in the order NAV,
  // ENG1, ENG2, HYD, TORQ, RPM, COM, JAM, RWR, HUD, LLTV, FLIR, GUN, HMTD, LAS, FUEL, U/C, FIRE,
  // each lit and then unlit; the TWI lamp is TWION.640 (index -1 here).
  struct Warning {
    int index;
    glm::vec2 at;
    bool on;
  };
  using S = sim::System;
  auto failed = [&](S s) { return d.failed[int(s)]; };
  const Warning warnings[] = {{17, {54, 272}, d.fire},
                              {1, {54, 288}, d.engineFailed[0]},
                              {2, {54, 304}, d.engineFailed[1]},
                              {4, {54, 320}, d.torque > 100.5f},
                              {5, {54, 336}, d.rotorRpm < 95.0f},
                              {10, {28, 288}, failed(S::Lltv)},
                              {11, {28, 304}, failed(S::Flir)},
                              {13, {28, 320}, failed(S::Hmtd)},
                              {14, {28, 336}, failed(S::Las)},
                              {12, {2, 304}, failed(S::Gun)},
                              {15, {2, 320}, d.fuelKg < 300.0f || d.fuelDamaged},
                              {16, {2, 336}, failed(S::Gear)},
                              {7, {155, 267}, failed(S::Jam)},
                              {0, {180, 267}, failed(S::Nav)},
                              {-1, {155, 283}, failed(S::Twi)},
                              {6, {180, 283}, failed(S::Com)},
                              {9, {180, 299}, failed(S::Hud)}};
  for (const Warning& w : warnings) {
    if (!w.on) continue;
    if (w.index < 0) {
      if (twiOn_.width) sprite(twiOn_, w.at);
      continue;
    }
    if (warning_.height >= 432) {
      sprite(warning_, w.at, 24 * w.index, 12);
    } else {
      alpha_ = 0.6f;
      rect(w.at, w.at + glm::vec2(20, 12), kRed);
      alpha_ = 1.0f;
    }
  }
  // Danger altitude lamp by the radar altimeter: below 15 m off the ground.
  if (awarnOn_.width && d.radarAltitude > 3.0f && d.radarAltitude < 15.0f) sprite(awarnOn_, {144, 464});
}

void PanelGauges::rpmAndFuel(const InstrumentData& d) {
  // Three bars from 0 to 120 %: left and right engine, rotor.
  auto bar = [&](float x, float pct, glm::vec3 col) {
    float top = 468.0f - std::clamp(pct, 0.0f, 120.0f) * 0.396f;
    if (top < 468.0f) rect({x, top}, {x + 2.0f, 468.0f}, col);
  };
  bar(18.0f, d.engineFailed[0] ? 0.0f : d.engineRpm, kAmber);
  bar(27.0f, d.engineFailed[1] ? 0.0f : d.engineRpm, kAmber);
  bar(44.0f, d.rotorRpm, glm::vec3(243, 127, 0) / 255.0f);
  // Fuel in kg on the four wheels of the counter left of the pitch/yaw/roll dial.
  drum(d.fuelKg, 4, {25.0f, 369.0f});
}

void PanelGauges::rwr(const InstrumentData& d) {
  if (!d.threat || rwrRing_.empty()) return;
  const glm::vec2 o{455.0f, 0.0f};
  float b = d.threatBearing, a = std::fabs(b);
  if (a <= 110.0f) {
    int step = a < 20.0f ? 0 : a < 40.0f ? 1 : a < 70.0f ? 2 : 3;
    if (b <= 5.0f) lit(rwrDir_[2 * step], o);
    if (b >= -5.0f) lit(rwrDir_[2 * step + 1], o);
  } else {
    lit(rwrDir_[b < 0.0f ? 8 : 9], o);
  }
  int n = int(std::ceil(std::clamp(d.threatProximity, 0.0f, 1.0f) * float(rwrRing_.size())));
  for (int i = 0; i < n; ++i) lit(rwrRing_[size_t(i)], o);
  if (d.threatClass >= 1 && d.threatClass <= 6) lit(rwrClass_[d.threatClass - 1], o);
}

void PanelGauges::weaponPanel(const InstrumentData& d) {
  // The WSO's right console: three counters for the inner and outer pylons and the wing tips, one
  // for the gun, each with a lamp lit for the selected weapon, and the weapon's name below. The
  // panel is turned, so everything steps down a pixel to the right.
  const glm::vec2 counters[4] = {{499, 395}, {546, 402}, {593, 409}, {484, 435}};
  const glm::vec2 lampsAt[4] = {{486, 394}, {533, 401}, {580, 408}, {471, 434}};
  for (int i = 0; i < 4; ++i) {
    int digits = i == 3 ? 4 : 3;
    int v = std::clamp(d.stationRounds[i], 0, i == 3 ? 9999 : 999);
    if (weapDigits_.height >= 80)
      for (int k = digits - 1; k >= 0; --k, v /= 10)
        sprite(weapDigits_, counters[i] + glm::vec2(7.0f * float(k), float(k)), 8 * (v % 10), 8);
    if (d.stationSelected[i] && liteOn_.width) sprite(liteOn_, lampsAt[i]);
  }
  // WEAP_TXT.640: an italic font of A..Z, 0..9, '-', '.' and space, 6 x 8 pixels a letter.
  if (weapFont_.height < 312) return;
  glm::vec2 at{542.0f, 446.0f};
  for (size_t i = 0; i < d.weapon.size() && i < 12; ++i) {
    char c = d.weapon[i];
    int g = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '0' && c <= '9' ? 26 + c - '0' : c == '-' ? 36 : c == '.' ? 37 : 38;
    sprite(weapFont_, glm::floor(at + glm::vec2(7.0f, 0.875f) * float(i)), 8 * g, 8);
  }
}

// ---- drawing

bool PanelGauges::endMission(int w, int h) {
  if (!endMission_.width) return false;
  // In the original's 640 x 480 screen, fitted to the window's height and centred.
  const float scale = float(h) / 480.0f;
  begin(w, h, (float(w) - 640.0f * scale) * 0.5f, scale);
  sprite(endMission_, {196.0f, 111.0f});
  flush();
  return true;
}

void PanelGauges::begin(int w, int h, float left, float scale, float drop) {
  w_ = w;
  h_ = h;
  left_ = left;
  scale_ = scale;
  drop_ = drop;
  verts_.clear();
}

void PanelGauges::flush(glm::vec3 light) {
  if (light != glm::vec3(1.0f))
    for (V& v : verts_) v.c = glm::vec4(glm::vec3(v.c) * light, v.c.a);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  prog_.use();
  glUniform2f(prog_.loc("uScreen"), float(w_), float(h_));
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(V)), verts_.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void PanelGauges::drawUnder(const InstrumentData& d, bool gunner, int w, int h, float left, float scale, float drop) {
  if (gunner) return;
  begin(w, h, left, scale, drop);
  glDisable(GL_DEPTH_TEST);
  // Moving map background straight from the terrain picture.
  glm::vec2 m0 = at(kMap0), m1 = at(kMap1);
  mapProg_.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, mapTex_);
  glUniform1i(mapProg_.loc("uMap"), 0);
  glUniform4f(mapProg_.loc("uRect"), m0.x, m1.y, m1.x, m0.y);
  const glm::vec2 margin{12.0f};
  glm::vec2 p = (kMap0 + kMap1) * 0.5f + (d.position - mapCentre_) / kMapMetresPerPixel;
  if (glm::any(glm::lessThan(p, kMap0 + margin)) || glm::any(glm::greaterThan(p, kMap1 - margin)))
    mapCentre_ = d.position;
  glUniform2f(mapProg_.loc("uCenter"), mapCentre_.x, mapCentre_.y);
  mapProg_.set("uMpp", kMapMetresPerPixel / scale);
  glUniform2f(mapProg_.loc("uOrigin"), mapOrigin_.x, mapOrigin_.y);
  mapProg_.set("uSize", mapSize_);
  mapProg_.set("uOutside", mapOutside_);
  mapProg_.set("uLight", d.mapLight);
  glBindVertexArray(mapVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  horizon(d);
  standbyHorizon(d);
  map(d);
  flush(d.dialLight);
}

void PanelGauges::drawOver(const InstrumentData& d, bool gunner, int w, int h, float left, float scale, float drop) {
  begin(w, h, left, scale, drop);
  if (gunner) {
    baroAltimeter(d, kGunBaro, 25.0f);
    verticalSpeed(d, kGunVsi, 25.0f);
    airspeed(d, kGunAirspeed, 25.0f);
    radarAltimeter(d, kGunRadar, 20.0f);
    flush(d.dialLight);
    begin(w, h, left, scale, drop);
    weaponPanel(d);  // its counters and lamps light themselves
  } else {
    gMeter(d, kGMeter, 15.0f);
    clock(d, kClock, 16.0f);
    airspeed(d, kAirspeed, 26.0f);
    verticalSpeed(d, kVsi, 25.0f);
    radarAltimeter(d, kRadar, 20.5f);
    baroAltimeter(d, kBaro, 26.0f);
    hsi(d);
    pitchYawRoll(d);
    rpmAndFuel(d);
    flush(d.dialLight);
    // The warning lamps and the threat warning panel light themselves.
    begin(w, h, left, scale, drop);
    lamps(d);
    rwr(d);
  }
  flush();
}

}  // namespace render
