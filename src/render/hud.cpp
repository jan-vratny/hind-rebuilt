#include "render/hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "render/font.hpp"
#include "render/shaders.hpp"

namespace render {

namespace {

glm::vec2 rot(glm::vec2 v, float a) {
  float c = std::cos(a), s = std::sin(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

std::string fmt(const char* f, double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, f, v);
  return buf;
}

const glm::vec4 kGreen{0.3f, 1.0f, 0.4f, 0.92f};
const glm::vec4 kDimGreen{0.3f, 1.0f, 0.4f, 0.55f};
const glm::vec4 kWarn{1.0f, 0.38f, 0.22f, 0.95f};

}  // namespace

Hud::~Hud() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Hud::init() {
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, p));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, c));
  return prog_.build("hud", shaders::kHudVS, shaders::kHudFS);
}

void Hud::line(glm::vec2 a, glm::vec2 b) {
  glm::vec2 d = b - a;
  float len = glm::length(d);
  if (len < 1e-4f) d = {1, 0};
  else d /= len;
  glm::vec2 n{-d.y, d.x};
  float h = thick_ * 0.5f;
  // Extend by half the width so joints of polylines close up.
  a -= d * h;
  b += d * h;
  glm::vec2 p0 = a + n * h, p1 = b + n * h, p2 = b - n * h, p3 = a - n * h;
  for (glm::vec2 p : {p0, p1, p2, p0, p2, p3}) verts_.push_back({p, color_});
}

void Hud::polyline(std::initializer_list<glm::vec2> pts) {
  const glm::vec2* prev = nullptr;
  for (const glm::vec2& p : pts) {
    if (prev) line(*prev, p);
    prev = &p;
  }
}

void Hud::rect(glm::vec2 a, glm::vec2 b) {
  for (glm::vec2 p : {a, glm::vec2(b.x, a.y), b, a, b, glm::vec2(a.x, b.y)}) verts_.push_back({p, color_});
}

void Hud::text(const std::string& s, glm::vec2 pos, float size, int align) {
  // Pixel font: each glyph pixel a whole number of screen pixels, aligned to the screen grid.
  float unit = std::max(1.0f, std::round(size / float(kGlyphH)));
  float advance = unit * (kGlyphW + 1);
  float width = s.empty() ? 0.0f : advance * float(s.size()) - unit;
  if (align == 0) pos.x -= width * 0.5f;
  else if (align > 0) pos.x -= width;
  pos = glm::floor(pos);
  for (char ch : s) {
    const GlyphRows& rows = glyphRows(ch);
    for (int r = 0; r < kGlyphH; ++r) {
      for (int c = 0; c < kGlyphW; ++c) {
        if (!(rows[size_t(r)] >> (kGlyphW - 1 - c) & 1)) continue;
        glm::vec2 p = pos + glm::vec2(c, kGlyphH - 1 - r) * unit;
        rect(p, p + glm::vec2(unit));
      }
    }
    pos.x += advance;
  }
}

void Hud::drawDroneView(const HudData& d, int w, int h) {
  // The drone camera's marks: corners of its frame, a cross with a gap in the middle, its readout
  // at the top, the flier nearest ahead in brackets with its name and range.
  const float s = h / 1080.0f, cx = w * 0.5f, cy = h * 0.5f;
  color_ = kGreen;
  const float fx = w * 0.42f, fy = h * 0.4f, k = 40.0f * s;
  for (float sx : {-1.0f, 1.0f})
    for (float sy : {-1.0f, 1.0f}) {
      const glm::vec2 c{cx + sx * fx, cy + sy * fy};
      line(c, c - glm::vec2(sx * k, 0.0f));
      line(c, c - glm::vec2(0.0f, sy * k));
    }
  for (const glm::vec2 dir : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)})
    line(glm::vec2(cx, cy) + dir * 8.0f * s, glm::vec2(cx, cy) + dir * 30.0f * s);
  text(d.droneInfo, {cx, h - 70.0f * s}, 24.0f * s, 0);
  text(d.droneKeys, {cx, h - 100.0f * s}, 20.0f * s, 0);
  if (!d.droneLocked) {
    // The mouse's stick: its travel a ring round the cross, a small diamond where it is held.
    const float r = 110.0f * s, q = 7.0f * s;
    color_ = kGreen * glm::vec4(1.0f, 1.0f, 1.0f, 0.35f);
    for (int i = 0; i < 32; ++i)
      line(glm::vec2(cx, cy) + r * glm::vec2(std::cos(i * 0.19635f), std::sin(i * 0.19635f)),
           glm::vec2(cx, cy) + r * glm::vec2(std::cos((i + 1) * 0.19635f), std::sin((i + 1) * 0.19635f)));
    color_ = kGreen;
    const glm::vec2 m{cx - d.droneStick.x * r, cy + d.droneStick.y * r};
    polyline({m + glm::vec2(q, 0), m + glm::vec2(0, q), m + glm::vec2(-q, 0), m + glm::vec2(0, -q), m + glm::vec2(q, 0)});
  }
  for (size_t i = 0; i < d.droneMarks.size(); ++i) {
    const glm::vec2 m = d.droneMarks[i].at;
    if (i > 0 || !d.droneSelected) {
      // Marked: four small corners.
      const float b = 12.0f * s, t = 5.0f * s;
      for (float sx : {-1.0f, 1.0f})
        for (float sy : {-1.0f, 1.0f})
          polyline({m + glm::vec2(sx * (b - t), sy * b), m + glm::vec2(sx * b, sy * b), m + glm::vec2(sx * b, sy * (b - t))});
      continue;
    }
    // Picked: brackets; near enough, a box; locked, the box with its diagonals' ends.
    const float b = 22.0f * s, t = 8.0f * s;
    if (d.droneLockable || d.droneLocked) {
      polyline({m + glm::vec2(-b, -b), m + glm::vec2(b, -b), m + glm::vec2(b, b), m + glm::vec2(-b, b), m + glm::vec2(-b, -b)});
    } else {
      for (float sx : {-1.0f, 1.0f})
        polyline({m + glm::vec2(sx * (b - t), b), m + glm::vec2(sx * b, b), m + glm::vec2(sx * b, -b), m + glm::vec2(sx * (b - t), -b)});
    }
    if (d.droneLocked)
      for (float sx : {-1.0f, 1.0f})
        for (float sy : {-1.0f, 1.0f}) line(m + glm::vec2(sx, sy) * b, m + glm::vec2(sx, sy) * (b + t));
    text(d.droneMarkText, m + glm::vec2(0.0f, -b - 30.0f * s), 20.0f * s, 0);
  }
}

void Hud::drawPage(const std::vector<std::string>& page, int w, int h) {
  // Dark glass over the view, the title large, the paragraphs wrapped to the column.
  const glm::vec4 green = color_;
  color_ = {0.0f, 0.03f, 0.0f, 0.82f};
  rect({0.0f, 0.0f}, {float(w), float(h)});
  color_ = green;
  const float size = std::max(14.0f, h / 45.0f), column = std::min(float(w) * 0.8f, size * 70.0f);
  const float left = (float(w) - column) * 0.5f, advance = std::max(1.0f, std::round(size / kGlyphH)) * (kGlyphW + 1);
  const size_t perLine = std::max<size_t>(10, size_t(column / advance));
  float y = h * 0.86f;
  for (size_t p = 0; p < page.size(); ++p) {
    if (p == 0) {
      text(page[0], {w * 0.5f, y}, size * 1.6f, 0);
      y -= size * 3.2f;
      continue;
    }
    std::string line;
    size_t start = 0;
    const std::string& para = page[p];
    while (start <= para.size()) {
      size_t end = para.find(' ', start);
      if (end == std::string::npos) end = para.size();
      std::string word = para.substr(start, end - start);
      if (!line.empty() && line.size() + 1 + word.size() > perLine) {
        text(line, {left, y}, size);
        y -= size * 1.6f;
        line.clear();
      }
      line += (line.empty() ? "" : " ") + word;
      start = end + 1;
    }
    if (!line.empty()) text(line, {left, y}, size);
    y -= size * 2.6f;
  }
}

void Hud::draw(const HudData& d, int w, int h) {
  verts_.clear();
  thick_ = std::max(1.5f, h / 540.0f);
  if (d.cueShown) {
    const float s = h / 1080.0f, k = 12.0f * s;
    const glm::vec2 t = d.cueMark;
    polyline({t + glm::vec2(0, k), t + glm::vec2(k, 0), t + glm::vec2(0, -k), t + glm::vec2(-k, 0), t + glm::vec2(0, k)});
    line(t + glm::vec2(-2, 0) * s, t + glm::vec2(2, 0) * s);
  }
  for (glm::vec2 f : d.friendMarks) {
    const float k = 9.0f * h / 1080.0f;
    circle(f, k, 12);
    line(f + glm::vec2(0.0f, k), f + glm::vec2(0.0f, k * 2.0f));
  }
  if (d.bowView) {
    const float s = h / 1080.0f;
    const glm::vec2 c{w * 0.5f, h * 0.5f};
    color_ = kGreen * glm::vec4(1.0f, 1.0f, 1.0f, 0.6f);
    if (d.bowCross)
      for (const glm::vec2 dir : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)})
        line(c + dir * 5.0f * s, c + dir * 14.0f * s);
    // The arrow's landing: a glowing ring, its blast's reach, on the ground.
    // Its flight there a thin dotted line, the ring red with something it can hit in it.
    const float thick = thick_;
    const glm::vec3 glow = d.bowRingHot ? glm::vec3(1.0f, 0.15f, 0.1f) : glm::vec3(1.0f, 0.62f, 0.15f);
    thick_ = 2.5f * s;
    color_ = glm::vec4(glow, 0.9f);
    for (const std::vector<glm::vec2>& part : d.bowPath)
      for (size_t k = 1; k < part.size(); k += 2) line(part[k - 1], part[k]);
    for (const auto& [width, alpha] : {std::pair{7.0f, 0.18f}, std::pair{3.5f, 0.35f}, std::pair{1.6f, 0.95f}}) {
      thick_ = width * s;
      color_ = glm::vec4(glow, alpha);
      for (const std::vector<glm::vec2>& part : d.bowRing)
        for (size_t k = 1; k < part.size(); ++k) line(part[k - 1], part[k]);
    }
    thick_ = thick;
    // The LAW's sight: the circle within which it locks on; round what it is locking on, corners
    // closing in on it (amber, blinking), locked a red square.
    if (d.lawCone > 0.0f) {
      color_ = kGreen * glm::vec4(1.0f, 1.0f, 1.0f, 0.45f);
      circle(c, d.lawCone, 40);
    }
    if (d.lawLock >= 0.0f) {
      const bool locked = d.lawLock >= 1.0f;
      const float k = (locked ? 22.0f : 22.0f + 30.0f * (1.0f - d.lawLock)) * s, e = 9.0f * s;
      thick_ = 2.0f * s;
      color_ = locked ? glm::vec4(1.0f, 0.15f, 0.1f, 0.95f)
                      : glm::vec4(1.0f, 0.62f, 0.15f, std::fmod(d.time * 6.0f, 1.0f) < 0.5f ? 0.95f : 0.45f);
      const glm::vec2 b = d.lawBox;
      for (const glm::vec2 q : {glm::vec2(1, 1), glm::vec2(-1, 1), glm::vec2(1, -1), glm::vec2(-1, -1)}) {
        const glm::vec2 corner = b + q * k;
        line(corner, corner - glm::vec2(q.x, 0.0f) * e);
        line(corner, corner - glm::vec2(0.0f, q.y) * e);
      }
      if (locked) {
        polyline({b + glm::vec2(-k, -k), b + glm::vec2(k, -k), b + glm::vec2(k, k), b + glm::vec2(-k, k), b + glm::vec2(-k, -k)});
        text("LOCKED", b + glm::vec2(0.0f, k + 10.0f * s), 18.0f * s, 0);
      }
      thick_ = thick;
    }
    color_ = kGreen;
    text(d.bowInfo, {c.x, 60.0f * s}, 24.0f * s, 0);
    if (d.bowHealth >= 0.0f) {
      // His health, bottom left: a bar, green, amber below a half, red below a quarter.
      const glm::vec2 at{40.0f * s, 110.0f * s}, size{260.0f * s, 16.0f * s};
      text("HEALTH", {at.x, at.y + size.y + 8.0f * s}, 20.0f * s, -1);
      color_ = kGreen * glm::vec4(1.0f, 1.0f, 1.0f, 0.25f);
      rect(at, at + size);
      color_ = d.bowHealth > 0.5f ? kGreen : d.bowHealth > 0.25f ? glm::vec4(1.0f, 0.75f, 0.2f, 0.95f) : glm::vec4(1.0f, 0.2f, 0.15f, 0.95f);
      rect(at, at + glm::vec2(size.x * std::clamp(d.bowHealth, 0.0f, 1.0f), size.y));
      color_ = kGreen;
    }
    if (!d.bowDead.empty()) {
      color_ = glm::vec4(1.0f, 0.25f, 0.2f, 0.95f);
      text(d.bowDead, c, 40.0f * s, 0);
      color_ = kGreen;
    }
  } else if (d.droneView) drawDroneView(d, w, h);
  else if (!d.watchHint.empty()) {}  // watching another Hind: nothing of our wreck's
  else if (!d.cockpit) drawExternalInfo(d, w, h);
  else if (d.gunner) drawGunnerSight(d, w, h);
  else if (d.headUp) {
    // On the collimator's glass: it slides with the cockpit picture as the pilot looks aside or up.
    const size_t from = verts_.size();
    drawCockpitHud(d, w, h);
    for (size_t i = from; i < verts_.size(); ++i) verts_[i].p += glm::vec2(d.hudShiftX, d.hudShiftY);
  }
  if (!d.watchHint.empty()) {
    color_ = kGreen;
    text(d.watchHint, {w * 0.5f, 60.0f * h / 1080.0f}, 20.0f * h / 1080.0f, 0);
  }
  if (!d.droneView && !d.bowView) drawControls(d, w, h);
  if (d.doorShotShown && !d.droneView) drawDoorShot(d, w, h);

  float s = h / 1080.0f;
  if (d.magnification > 1.01f) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.3gX", d.magnification);
    color_ = kGreen;
    text(buf, {w * 0.5f, h * 0.5f + 230.0f * s}, 26.0f * s, 0);
  }
  if (!d.doorGun.empty()) {
    // The door gun's ring sight and bead, and its rounds below.
    color_ = kGreen;
    if (d.doorMarkShown) {
      circle(d.doorMark, 26.0f * s);
      circle(d.doorMark, 2.5f * s, 8);
    }
    text(d.doorGun, {w * 0.5f, 60.0f * s}, 24.0f * s, 0);
  }
  // I: the HUD and the sights black, as the original can have them (the text display below stays lit).
  if (d.black)
    for (V& v : verts_)
      if (v.c.g > v.c.r * 1.5f) v.c = glm::vec4(glm::vec3(0.03f, 0.05f, 0.03f), std::min(1.0f, v.c.a + 0.05f));
  color_ = kGreen;
  const glm::vec4& r = d.displayRect;
  if (r.z > r.x && !d.displayText.empty()) {
    // Two lines of letters a quarter of the display high; words wrap onto the second line.
    const float size = (r.w - r.y) * 0.225f;
    const size_t perLine = std::max<size_t>(1, size_t((r.z - r.x) * 0.92f / (size * 5.5f / 6.0f)));
    // Each '\n'-separated part starts a line; at most the last two lines are shown. The Hip's
    // call ("HIP: ...", the upper line while its drone flies) is not wrapped nor pushed off: its
    // letters are made smaller to fit.
    std::vector<std::string> lines;
    std::string hip;
    size_t from = 0;
    while (from <= d.displayText.size()) {
      size_t nl = d.displayText.find('\n', from);
      if (nl == std::string::npos) nl = d.displayText.size();
      std::string part = d.displayText.substr(from, nl - from);
      from = nl + 1;
      if (from == 1 + part.size() && part.rfind("HIP: ", 0) == 0) {
        hip = part;
        continue;
      }
      lines.emplace_back();
      size_t start = 0;
      while (start < part.size()) {
        size_t end = part.find(' ', start);
        if (end == std::string::npos) end = part.size();
        std::string word = part.substr(start, end - start);
        if (!lines.back().empty() && lines.back().size() + 1 + word.size() > perLine) lines.push_back(word);
        else lines.back() += (lines.back().empty() ? "" : " ") + word;
        start = end + 1;
      }
    }
    const size_t keep = hip.empty() ? 2 : 1;
    if (lines.size() > keep) lines.erase(lines.begin(), lines.end() - keep);
    if (!hip.empty()) lines.insert(lines.begin(), hip);
    const float gap = size * 1.5f, mid = (r.y + r.w) * 0.5f;
    for (size_t i = 0; i < lines.size(); ++i) {
      const bool fit = !hip.empty() && i == 0;
      std::string line = fit ? lines[i] : lines[i].substr(0, perLine);
      const float sz = fit ? size * std::min(1.0f, float(perLine) / float(std::max<size_t>(1, line.size()))) : size;
      float y = mid - size * 0.5f + (float(lines.size()) - 1.0f) * gap * 0.5f - float(i) * gap + (size - sz) * 0.5f;
      text(line, {(r.x + r.z) * 0.5f, y}, sz, 0);
    }
  }
  if (d.dim > 0.0f) {
    // Paused: the whole picture to about half its brightness (0.49 measured on the original's).
    color_ = {0.0f, 0.0f, 0.0f, 0.51f * d.dim};
    rect({0.0f, 0.0f}, {float(w), float(h)});
    color_ = kGreen;
  }
  if (!d.message.empty()) text(d.message, {w * 0.5f, h - 120.0f * s}, 30.0f * s, 0);
  if (d.score >= 0 && d.page.empty()) text("SCORE " + std::to_string(d.score), {w - 30.0f * s, h - 50.0f * s}, 26.0f * s, 1);
  if (d.timeScale > 1 && d.page.empty()) text("TIME X" + std::to_string(d.timeScale), {w * 0.5f, h - 160.0f * s}, 26.0f * s, 0);
  if (!d.page.empty()) drawPage(d.page, w, h);

  if (verts_.empty()) return;
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  prog_.use();
  glUniform2f(prog_.loc("uScreen"), float(w), float(h));
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(V)), verts_.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void Hud::drawCockpitHud(const HudData& d, int w, int h) {
  // Sized to the collimator glass above the instrument panel and centred on the aircraft's axis.
  const float s = h / 1080.0f * 0.6f;
  const bool rotorBad = d.rotorFree && (d.rotorRpm < 90.0f || d.rotorRpm > 112.0f);
  const glm::vec2 c{w * 0.5f, h * 0.5f + d.boresightUp};
  const float focal = (h * 0.5f) / std::tan(d.fovY * 0.5f);
  color_ = kGreen;

  // HUD centre datum: fixed cross.
  line(c + glm::vec2(-14, 0) * s, c + glm::vec2(14, 0) * s);
  line(c + glm::vec2(0, -14) * s, c + glm::vec2(0, 14) * s);

  // Aircraft datum: rotates about the centre with the bank angle.
  float bank = -glm::radians(d.rollDeg);
  auto ac = [&](float x, float y) { return c + rot(glm::vec2(x, y) * s, bank); };
  line(ac(-120, 0), ac(-40, 0));
  line(ac(40, 0), ac(120, 0));
  line(ac(-120, 0), ac(-120, -12));
  line(ac(120, 0), ac(120, -12));
  line(ac(0, 22), ac(0, 40));

  // Horizon line: rises and falls with pitch, stays level.
  float hy = c.y - focal * std::tan(glm::radians(std::clamp(d.pitchDeg, -80.0f, 80.0f)));
  hy = std::clamp(hy, c.y - 280.0f * s, c.y + 280.0f * s);
  line({c.x - 330.0f * s, hy}, {c.x - 150.0f * s, hy});
  line({c.x + 150.0f * s, hy}, {c.x + 330.0f * s, hy});
  line({c.x - 150.0f * s, hy}, {c.x - 150.0f * s, hy - 12.0f * s});
  line({c.x + 150.0f * s, hy}, {c.x + 150.0f * s, hy - 12.0f * s});
  text(fmt("%+03.0f", d.pitchDeg), {c.x - 345.0f * s, hy - 9.0f * s}, 18.0f * s, 1);

  // Roll scale: semicircle below the centre, ticks at 5/10/15/30/60 degrees.
  const float R = 210.0f * s;
  for (float a : {-60.f, -30.f, -15.f, -10.f, -5.f, 0.f, 5.f, 10.f, 15.f, 30.f, 60.f}) {
    float t = glm::radians(a);
    glm::vec2 dir{std::sin(t), -std::cos(t)};
    float len = (a == 0.f || std::fabs(a) == 30.f || std::fabs(a) == 60.f) ? 18.0f : 10.0f;
    line(c + dir * R, c + dir * (R + len * s));
  }
  {
    float t = std::clamp(-glm::radians(d.rollDeg), -glm::radians(70.0f), glm::radians(70.0f));
    glm::vec2 dir{std::sin(t), -std::cos(t)}, side{std::cos(t), std::sin(t)};
    glm::vec2 tip = c + dir * (R - 3.0f * s);
    polyline({tip, tip - dir * 14.0f * s + side * 8.0f * s, tip - dir * 14.0f * s - side * 8.0f * s, tip});
  }

  // Heading tape, labels in tens of degrees.
  const float ty = c.y + 330.0f * s, ppd = 6.0f * s;
  line({c.x - 220.0f * s, ty}, {c.x + 220.0f * s, ty});
  int first = int(std::floor((d.headingDeg - 36.0f) / 5.0f)) * 5;
  for (int deg = first; deg <= d.headingDeg + 36.0f; deg += 5) {
    float x = c.x + (deg - d.headingDeg) * ppd;
    if (std::fabs(x - c.x) > 220.0f * s) continue;
    int norm = ((deg % 360) + 360) % 360;
    line({x, ty}, {x, ty + (norm % 10 == 0 ? 14.0f : 7.0f) * s});
    if (norm % 30 == 0) text(std::to_string(norm / 10), {x, ty + 22.0f * s}, 16.0f * s, 0);
  }
  polyline({{c.x, ty - 2.0f * s}, {c.x - 8.0f * s, ty - 14.0f * s}, {c.x + 8.0f * s, ty - 14.0f * s}, {c.x, ty - 2.0f * s}});
  // The course to the next waypoint: a small caret under the tape, held at the nearer end when
  // it is off the tape (as in the original).
  if (d.waypointShown) {
    const float off = std::remainder(d.waypointBearing - d.headingDeg, 360.0f);
    const float x = c.x + std::clamp(off * ppd, -220.0f * s, 220.0f * s);
    polyline({{x - 7.0f * s, ty - 17.0f * s}, {x, ty - 7.0f * s}, {x + 7.0f * s, ty - 17.0f * s}});
  }
  if (d.weaponsMode) {
    // Weapons mode: speed and altitude at the ends of the tape, torque under it, the weapon
    // symbols instead of the navigation aids.
    text(fmt("%.0f", d.forwardKmh), {c.x - 230.0f * s, ty - 30.0f * s}, 22.0f * s, 1);
    text(fmt(d.radarAltitude ? "R:%.0f" : "%.0f", d.altitude), {c.x + 230.0f * s, ty - 30.0f * s}, 22.0f * s, -1);
    color_ = d.torque > 100.5f || rotorBad ? kWarn : kGreen;
    text(d.rotorFree ? fmt("RPM %.0f", d.rotorRpm) : fmt("%.0f", d.torque), {c.x, ty - 44.0f * s}, 22.0f * s, 0);
    color_ = kGreen;
    drawWeaponSymbols(d, c, s, c.x - 300.0f * s, c.y - 230.0f * s);
    return;
  }
  int hdgTens = int(std::lround(d.headingDeg / 10.0f)) % 36;
  text(fmt("%02.0f", hdgTens), {c.x, ty - 44.0f * s}, 22.0f * s, 0);

  // Forward speed with its sign (negative when moving backwards), and altitude on the right.
  glm::vec2 sp{c.x - 330.0f * s, c.y + 80.0f * s};
  text(fmt("%.0f", d.forwardKmh), sp, 26.0f * s, 1);
  text("KM/H", sp + glm::vec2(0, -26.0f * s), 13.0f * s, 1);
  glm::vec2 al{c.x + 330.0f * s, c.y + 80.0f * s};
  text(fmt("%.0f", d.altitude), al, 26.0f * s, -1);
  text(d.radarAltitude ? "M  R" : "M", al + glm::vec2(0, -26.0f * s), 13.0f * s, -1);

  // Hover drift: below 60 km/h a line from the centre shows where the helicopter is sliding
  // over the ground (up = forward, full circle = 30 km/h), with the sideways speed under it.
  if (std::fabs(d.forwardKmh) < 60.0f && std::fabs(d.sideKmh) < 60.0f) {
    const float R = 120.0f * s;
    glm::vec2 v = glm::vec2(d.sideKmh, d.forwardKmh) / 30.0f;
    if (glm::length(v) > 1.0f) v = glm::normalize(v);
    glm::vec2 tip = c + v * R;
    color_ = kDimGreen;
    const int n = 40;
    for (int i = 0; i < n; i += 2) {  // dashed range circle
      float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
      line(c + R * glm::vec2(std::cos(a0), std::sin(a0)), c + R * glm::vec2(std::cos(a1), std::sin(a1)));
    }
    color_ = kGreen;
    line(c, tip);
    polyline({tip + glm::vec2(-6, -6) * s, tip + glm::vec2(6, -6) * s, tip + glm::vec2(6, 6) * s,
              tip + glm::vec2(-6, 6) * s, tip + glm::vec2(-6, -6) * s});
    if (std::fabs(d.sideKmh) >= 1.0f)
      text(fmt(d.sideKmh > 0 ? "R%.0f" : "L%.0f", std::fabs(d.sideKmh)), sp + glm::vec2(0, -52.0f * s), 18.0f * s, 1);
  }

  // Vertical speed scale (+-10 m/s).
  const float vx = c.x + 300.0f * s, vh = 120.0f * s;
  line({vx, c.y - vh}, {vx, c.y + vh});
  for (int i = -10; i <= 10; i += 2) {
    float y = c.y + i * vh / 10.0f;
    line({vx, y}, {vx - (i == 0 ? 16.0f : 8.0f) * s, y});
  }
  float vy = c.y + std::clamp(d.verticalSpeed, -10.0f, 10.0f) * vh / 10.0f;
  polyline({{vx + 2.0f * s, vy}, {vx + 14.0f * s, vy + 7.0f * s}, {vx + 14.0f * s, vy - 7.0f * s}, {vx + 2.0f * s, vy}});
  text(fmt("%+.1f", d.verticalSpeed), {vx + 20.0f * s, vy - 7.0f * s}, 14.0f * s, -1);

  // Torque.
  bool over = d.torque > 100.5f || rotorBad;
  color_ = over ? kWarn : kGreen;
  glm::vec2 tq{c.x - 330.0f * s, c.y - 170.0f * s};
  text(fmt("%.0f", d.rotorFree ? d.rotorRpm : d.torque), tq, 26.0f * s, 1);
  text(d.rotorFree ? "RPM %" : "TQ %", tq + glm::vec2(0, -26.0f * s), 13.0f * s, 1);
  color_ = kGreen;
  if (!d.weapon.empty()) text(d.weapon, {c.x + 330.0f * s, c.y - 170.0f * s}, 20.0f * s, -1);
  if (!d.target.empty()) text(d.target, {c.x + 330.0f * s, c.y - 200.0f * s}, 16.0f * s, -1);

  if (d.waypointShown && d.waypointLetter) {
    // The flight path marker: a ring in the original's pixels, held at the edge of the HUD.
    const float px = std::max(2.0f, std::round(3.0f * s)), ppd = 8.0f * s;
    const glm::vec2 at = c + glm::clamp(d.pathMarkerDeg * ppd, glm::vec2(-260.0f, -200.0f) * s, glm::vec2(260.0f, 200.0f) * s);
    pixelCircle(at, 14, px);
    // The waypoint, its distance and the time to it.
    text(std::string(1, d.waypointLetter) + fmt("  %.1f", d.waypointKm), {c.x - 330.0f * s, c.y - 260.0f * s}, 20.0f * s, -1);
    const int sec = int(d.waypointSeconds);
    text(d.waypointSeconds < 0.0f || sec > 5999 ? std::string("---") : fmt("%02.0f", sec / 60) + fmt(":%02.0f", sec % 60),
         {c.x + 330.0f * s, c.y - 260.0f * s}, 20.0f * s, 1);
  }
}

void Hud::pixelCircle(glm::vec2 c, int r, float px, float sweep) {
  c = glm::round(c / px) * px;
  auto dot = [&](int x, int y) {
    // Clockwise from 12 o'clock (y up).
    float a = std::atan2(float(x), float(y));
    if (a < 0.0f) a += 6.2831853f;
    if (a <= sweep + 1e-3f) rect(c + glm::vec2(x, y) * px, c + glm::vec2(x + 1, y + 1) * px);
  };
  // Midpoint circle, the eight octants.
  for (int x = 0, y = r, e = 1 - r; x <= y; ++x) {
    for (glm::ivec2 p : {glm::ivec2(x, y), glm::ivec2(y, x), glm::ivec2(y, -x), glm::ivec2(x, -y), glm::ivec2(-x, -y),
                         glm::ivec2(-y, -x), glm::ivec2(-y, x), glm::ivec2(-x, y)})
      dot(p.x, p.y);
    if (e < 0) e += 2 * x + 3;
    else e += 2 * (x - y) + 5, --y;
  }
}

void Hud::pixelLine(glm::vec2 a, glm::vec2 b, float px) {
  const glm::vec2 from = glm::round(a / px), to = glm::round(b / px);
  const int n = int(std::max(std::fabs(to.x - from.x), std::fabs(to.y - from.y)));
  for (int i = 0; i <= n; ++i) {
    const glm::vec2 p = glm::round(glm::mix(from, to, n ? float(i) / float(n) : 0.0f)) * px;
    rect(p, p + glm::vec2(px));
  }
}

void Hud::circle(glm::vec2 m, float r, int n) {
  for (int i = 0; i < n; ++i) {
    float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
    line(m + r * glm::vec2(std::cos(a0), std::sin(a0)), m + r * glm::vec2(std::cos(a1), std::sin(a1)));
  }
}

// Weapons mode, as the original's manual shows it (figures 5.3 and 8.0): the weapon range scale
// on the left (the weapon's minimum and maximum range, an arrow at the target's range while the
// laser has it), the weapon and its rounds under it, the aiming reticle on the target (with
// cross-hairs while the laser designates it), for bombs the impact point (CCIP) or, with the
// computer releasing them, the small bomb-aiming reticle, and a countdown at the lower right.
void Hud::drawWeaponSymbols(const HudData& d, glm::vec2 c, float s, float sx, float labelY) {
  const float top = c.y + 170.0f * s, bottom = c.y - 170.0f * s;
  const float full = std::max(d.maxRange * 1.25f, 100.0f);
  auto at = [&](float m) { return bottom + (top - bottom) * std::clamp(m / full, 0.0f, 1.0f); };
  line({sx, bottom}, {sx, top});
  for (int i = 0; i <= 6; ++i) line({sx, bottom + (top - bottom) * i / 6.0f}, {sx + 10.0f * s, bottom + (top - bottom) * i / 6.0f});
  for (float m : {d.minRange, d.maxRange})
    if (m > 0.0f) line({sx - 8.0f * s, at(m)}, {sx + 18.0f * s, at(m)});
  if (d.targetRange >= 0.0f) {
    const float y = at(d.targetRange);
    polyline({{sx - 4.0f * s, y}, {sx - 18.0f * s, y + 7.0f * s}, {sx - 18.0f * s, y - 7.0f * s}, {sx - 4.0f * s, y}});
  }
  text(d.weaponLabel, {sx - 10.0f * s, labelY}, 20.0f * s, -1);
  if (d.countdown >= 0.0f) text(fmt("%.0f", std::ceil(d.countdown)), {c.x + 260.0f * s, labelY}, 22.0f * s, -1);

  const float px = std::max(2.0f, std::round(3.0f * s));
  if (d.sight == HudData::Sight::Bombs) {
    if (d.bombComputer) {
      // The small bomb-aiming reticle: fly it over the HUD centre.
      const glm::vec2 b{c.x + std::clamp(d.bombLateral, -250.0f * s, 250.0f * s), c.y};
      circle(b, 10.0f * s, 16);
      line(b + glm::vec2(-16, 0) * s, b + glm::vec2(16, 0) * s);
      line(b + glm::vec2(0, -16) * s, b + glm::vec2(0, 16) * s);
    } else if (d.bombShown) {
      pixelCircle(d.bombMark, 8, px);
      rect(glm::round(d.bombMark / px) * px, glm::round(d.bombMark / px) * px + glm::vec2(px));
    }
  }
  // The aiming reticle on the target, in the original's pixels: out of the weapon's reach a ring
  // with a cross; within it a "pie" from 12 o'clock that shrinks as the target comes nearer.
  if (d.targetShown) {
    const glm::vec2 t = glm::round(d.targetMark / px) * px;
    const int R = 9;
    if (d.targetRange >= 0.0f && d.targetRange <= d.maxRange) {
      const float sweep = 6.2831853f * std::clamp(d.targetRange / std::max(d.maxRange, 1.0f), 0.03f, 1.0f);
      pixelCircle(t, R, px, sweep);
      for (float a : {0.0f, sweep}) {
        const glm::vec2 dir{std::sin(a), std::cos(a)};
        pixelLine(t + dir * float(R - 3) * px, t + dir * float(R + 3) * px, px);
      }
    } else {
      pixelCircle(t, R, px);
      for (glm::vec2 dir : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)})
        pixelLine(t + dir * float(R + 1) * px, t + dir * float(R + 4) * px, px);
    }
    // The R-60's seeker has it: cross-hairs over the reticle (as in the original).
    if (d.r60 && d.r60Lock) {
      pixelLine(t + glm::vec2(-float(R + 6), 0.0f) * px, t + glm::vec2(float(R + 6), 0.0f) * px, px);
      pixelLine(t + glm::vec2(0.0f, -float(R + 6)) * px, t + glm::vec2(0.0f, float(R + 6)) * px, px);
    }
  }
}

// Box around the tracked target; cross-hairs in it once the laser designator has the target
// (with the R-60 selected: once its seeker has it, as in the original).
void Hud::drawTargetBox(const HudData& d, float s) {
  if (!d.targetShown) return;
  const glm::vec2 t = d.targetMark;
  const float k = 16.0f * s;
  polyline({t + glm::vec2(-k, -k), t + glm::vec2(k, -k), t + glm::vec2(k, k), t + glm::vec2(-k, k), t + glm::vec2(-k, -k)});
  if (d.r60 ? d.r60Lock : d.lasing) {
    line(t + glm::vec2(-k - 10.0f * s, 0), t + glm::vec2(-k * 0.4f, 0));
    line(t + glm::vec2(k * 0.4f, 0), t + glm::vec2(k + 10.0f * s, 0));
    line(t + glm::vec2(0, -k - 10.0f * s), t + glm::vec2(0, -k * 0.4f));
    line(t + glm::vec2(0, k * 0.4f), t + glm::vec2(0, k + 10.0f * s));
  }
}

void Hud::drawGunnerSight(const HudData& d, int w, int h) {
  // Helmet mounted sight: the targeting circle stays in the middle of the view.
  const float s = h / 1080.0f;
  const glm::vec2 c{w * 0.5f, h * 0.5f};
  color_ = kGreen;
  if (d.weaponsMode) {
    // Weapons mode, as in the original: a cross in the middle, speed and altitude above, the
    // weapon symbols.
    line(c + glm::vec2(-12, 0) * s, c + glm::vec2(12, 0) * s);
    line(c + glm::vec2(0, -12) * s, c + glm::vec2(0, 12) * s);
    text(fmt("%.0f", d.speedKmh), {c.x - 330.0f * s, c.y + 215.0f * s}, 20.0f * s, -1);
    text(fmt(d.radarAltitude ? "R:%.0f" : "%.0f", d.altitude), {c.x + 200.0f * s, c.y + 215.0f * s}, 20.0f * s, -1);
    drawWeaponSymbols(d, c, s, c.x - 300.0f * s, c.y - 230.0f * s);
    if (!d.target.empty()) text(d.target, {c.x + 200.0f * s, c.y - 230.0f * s}, 16.0f * s, -1);
  } else {
    const int n = 48;
    const float r = 70.0f * s;
    for (int i = 0; i < n; ++i) {
      float a0 = 2.0f * 3.14159265f * i / n, a1 = 2.0f * 3.14159265f * (i + 1) / n;
      line(c + r * glm::vec2(std::cos(a0), std::sin(a0)), c + r * glm::vec2(std::cos(a1), std::sin(a1)));
    }
    for (glm::vec2 dir : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)}) {
      line(c + dir * (r - 14.0f * s), c + dir * (r + 22.0f * s));
    }
    line(c + glm::vec2(-6, 0) * s, c + glm::vec2(6, 0) * s);
    line(c + glm::vec2(0, -6) * s, c + glm::vec2(0, 6) * s);
    // Altitude scale on the left, like the original's sight symbology.
    const float sx = c.x - 330.0f * s, sh = 140.0f * s;
    line({sx, c.y - sh}, {sx, c.y + sh});
    for (int i = -4; i <= 4; ++i) line({sx, c.y + i * sh / 4.0f}, {sx + (i == 0 ? 18.0f : 9.0f) * s, c.y + i * sh / 4.0f});
    text(fmt("%.0f", d.altitude), {sx - 10.0f * s, c.y + sh + 14.0f * s}, 18.0f * s, -1);
    text(fmt("%.0f", d.speedKmh), {sx - 10.0f * s, c.y - sh - 34.0f * s}, 18.0f * s, -1);
    int hdg = int(std::lround(d.headingDeg)) % 360;
    text(fmt("%03.0f", hdg), {c.x, c.y + r + 40.0f * s}, 18.0f * s, 0);
    if (!d.weapon.empty()) text(d.weapon, {c.x, c.y - r - 60.0f * s}, 18.0f * s, 0);
    if (!d.target.empty()) text(d.target, {c.x, c.y - r - 88.0f * s}, 16.0f * s, 0);
    drawTargetBox(d, s);
  }
  // The gun's own direction: a small diamond, apart from the sight while the turret slews or
  // when the sight looks beyond the turret's limits.
  if (d.turretShown && glm::distance(d.turretMark, c) > 4.0f * s) {
    glm::vec2 t = d.turretMark;
    const float k = 9.0f * s;
    polyline({t + glm::vec2(0, k), t + glm::vec2(k, 0), t + glm::vec2(0, -k), t + glm::vec2(-k, 0), t + glm::vec2(0, k)});
  }
}

void Hud::drawExternalInfo(const HudData& d, int w, int h) {
  (void)w;
  const float s = h / 1080.0f;
  color_ = kGreen;
  float x = 30.0f * s, y = h - 50.0f * s, sz = 18.0f * s, gap = 30.0f * s;
  text(fmt("SPD %4.0f KM/H", d.backwards ? -d.speedKmh : d.speedKmh), {x, y}, sz);
  text(fmt("ALT %4.0f M", d.altitude) + (d.radarAltitude ? " R" : ""), {x, y - gap}, sz);
  text(fmt("HDG  %03.0f", std::fmod(std::round(d.headingDeg), 360.0)), {x, y - 2 * gap}, sz);
  text(fmt("V/S %+5.1f", d.verticalSpeed), {x, y - 3 * gap}, sz);
  color_ = d.torque > 100.5f || (d.rotorFree && (d.rotorRpm < 90.0f || d.rotorRpm > 112.0f)) ? kWarn : kGreen;
  text(d.rotorFree ? fmt("RPM %4.0f %%", d.rotorRpm) : fmt("TQ  %4.0f %%", d.torque), {x, y - 4 * gap}, sz);
  color_ = kGreen;
  std::string st = d.flightModel;
  if (d.gearMoving) st += "  GEAR MOVING";
  else if (!d.gearDown) st += "  GEAR UP";
  if (d.brakes) st += "  BRAKE";
  text(st, {x, y - 5 * gap}, 14.0f * s);
  if (!d.weapon.empty()) text(d.weapon, {x, y - 6 * gap}, 14.0f * s);
  if (!d.target.empty()) text(d.target, {x, y - 7 * gap}, 14.0f * s);
  std::string cm = fmt("FLR %.0f", d.flares) + fmt("  CHF %.0f", d.chaff) + (d.jammer ? "  JAM" : "");
  if (d.airframe < 100) cm += fmt("  DMG %.0f %%", 100 - d.airframe);
  color_ = d.airframe < 50 ? kWarn : kGreen;
  text(cm, {x, y - 8 * gap}, 14.0f * s);
  color_ = kGreen;
}

void Hud::drawDoorShot(const HudData& d, int w, int h) {
  // Top right: the door gunners' shot at the drone, and under it the range, the speed and the
  // height, each with a bar of how good it is and the number that says what to do.
  const float s = h / 1080.0f, size = 20.0f * s, gap = 30.0f * s;
  const float x = w - 420.0f * s, bar = x + 230.0f * s, barW = 150.0f * s;
  float y = h - 70.0f * s;
  color_ = kGreen;
  auto meter = [&](float value, float mark) {
    const glm::vec2 a{bar, y - 2.0f * s}, b{bar + barW, y + 14.0f * s};
    polyline({a, {b.x, a.y}, b, {a.x, b.y}, a});
    const float fill = std::clamp(value, 0.0f, 1.0f) * (barW - 8.0f * s);
    if (fill > 0.5f) rect(a + glm::vec2(4.0f * s), {a.x + 4.0f * s + fill, b.y - 4.0f * s});
    if (mark > 0.0f) line({bar + mark * barW, y - 7.0f * s}, {bar + mark * barW, y + 19.0f * s});
  };
  char buf[48];
  if (!d.doorShotSide) {
    text("DOOR  NOT IN FIELD", {x, y}, size);
    return;
  }
  std::snprintf(buf, sizeof buf, "DOOR %c  %s", d.doorShotSide,
                d.doorShotHold ? "HOLD" : d.doorShot >= d.doorOpenFire ? "FIRE" : "WAIT");
  text(buf, {x, y}, size);
  meter(d.doorShot, d.doorOpenFire);
  y -= gap;
  std::snprintf(buf, sizeof buf, "RNG %4.0f M", d.doorRange);
  text(buf, {x, y}, size);
  meter(d.doorNear, 0.0f);
  y -= gap;
  if (std::fabs(d.doorUpMs) >= 1.0f) std::snprintf(buf, sizeof buf, "SPD %+4.0f V%+.0f", d.doorAlongKmh, d.doorUpMs);
  else std::snprintf(buf, sizeof buf, "SPD %+4.0f KM/H", d.doorAlongKmh);
  text(buf, {x, y}, size);
  meter(d.doorMatched, 0.0f);
  y -= gap;
  std::snprintf(buf, sizeof buf, "LOW %4.0f M", d.doorBelowM);
  text(buf, {x, y}, size);
  meter(d.doorBelow, 0.0f);
}

void Hud::drawControls(const HudData& d, int w, int h) {
  // Control positions, a help for keyboard flying: cyclic box, pedal bar, collective bar.
  const float s = h / 1080.0f;
  color_ = kDimGreen;
  const float b = 45.0f * s;
  glm::vec2 c{w - 110.0f * s, 120.0f * s};
  polyline({c + glm::vec2(-b, -b), c + glm::vec2(b, -b), c + glm::vec2(b, b), c + glm::vec2(-b, b), c + glm::vec2(-b, -b)});
  line(c + glm::vec2(-6, 0) * s, c + glm::vec2(6, 0) * s);
  line(c + glm::vec2(0, -6) * s, c + glm::vec2(0, 6) * s);
  glm::vec2 cp = c + glm::vec2(d.cyclicX, d.cyclicY) * b;
  color_ = kGreen;
  polyline({cp + glm::vec2(-5, -5) * s, cp + glm::vec2(5, -5) * s, cp + glm::vec2(5, 5) * s, cp + glm::vec2(-5, 5) * s,
            cp + glm::vec2(-5, -5) * s});
  // The cyclic keys held: a mark outside the box on that side (as the cross moves).
  const glm::vec2 sides[4] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  for (int k = 0; k < 4; ++k) {
    if (!(d.cyclicKeys & (1u << k))) continue;
    const glm::vec2 n = sides[k], t{-n.y, n.x};
    const glm::vec2 tip = c + n * (b + 11.0f * s), base = c + n * (b + 4.0f * s);
    polyline({base + t * 5.0f * s, tip, base - t * 5.0f * s, base + t * 5.0f * s});
  }
  // Pedals.
  color_ = kDimGreen;
  float py = c.y - b - 22.0f * s;
  line({c.x - b, py}, {c.x + b, py});
  line({c.x, py - 5.0f * s}, {c.x, py + 5.0f * s});
  color_ = kGreen;
  float px = c.x + d.pedal * b;
  line({px, py - 9.0f * s}, {px, py + 9.0f * s});
  // Collective (0..130 %, mark at 100 %).
  color_ = kDimGreen;
  float cx = c.x - b - 26.0f * s;
  float y0 = c.y - b, y1 = c.y + b;
  line({cx, y0}, {cx, y1});
  float y100 = y0 + (y1 - y0) / 1.3f;
  line({cx - 6.0f * s, y100}, {cx + 6.0f * s, y100});
  color_ = d.collective > 1.005f ? kWarn : kGreen;
  float cy = y0 + (y1 - y0) * std::clamp(d.collective / 1.3f, 0.0f, 1.0f);
  line({cx - 10.0f * s, cy}, {cx + 10.0f * s, cy});
  color_ = kDimGreen;
  text(d.joystick ? "JOY" : "KBD", {c.x, c.y - b - 52.0f * s}, 12.0f * s, 0);
}

}  // namespace render
