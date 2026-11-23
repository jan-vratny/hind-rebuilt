#include "menu/raster.hpp"

#include <algorithm>
#include <cmath>

namespace menu {

void Raster::line(glm::vec2 a, glm::vec2 b, glm::u8vec3 c) {
  // Cut to the picture (Liang-Barsky), then Bresenham.
  const glm::vec2 d = b - a;
  float t0 = 0.0f, t1 = 1.0f;
  const float p[4] = {-d.x, d.x, -d.y, d.y};
  const float q[4] = {a.x + 1.0f, float(w_) - a.x, a.y + 1.0f, float(h_) - a.y};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0f) {
      if (q[i] < 0.0f) return;
      continue;
    }
    const float t = q[i] / p[i];
    if (p[i] < 0.0f) t0 = std::max(t0, t);
    else t1 = std::min(t1, t);
  }
  if (t0 > t1) return;
  const glm::vec2 from = a + d * t0, to = a + d * t1;
  int x0 = int(std::floor(from.x)), y0 = int(std::floor(from.y));
  const int x1 = int(std::floor(to.x)), y1 = int(std::floor(to.y));
  const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    set(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}

void Raster::circle(glm::vec2 centre, float r, glm::u8vec3 c) {
  // Midpoint circle.
  const int cx = int(std::floor(centre.x)), cy = int(std::floor(centre.y));
  int x = int(std::lround(r)), y = 0, err = 1 - x;
  if (x <= 0) {
    set(cx, cy, c);
    return;
  }
  if (cx + x < 0 || cy + x < 0 || cx - x >= w_ || cy - x >= h_) return;
  while (x >= y) {
    const int pts[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
    for (const auto& p : pts) set(cx + p[0], cy + p[1], c);
    ++y;
    if (err < 0) err += 2 * y + 1;
    else --x, err += 2 * (y - x) + 1;
  }
}

void Raster::disc(glm::vec2 centre, float r, glm::u8vec3 c) {
  const int cx = int(std::floor(centre.x)), cy = int(std::floor(centre.y)), ri = int(std::lround(r));
  for (int y = -ri; y <= ri; ++y)
    for (int x = -ri; x <= ri; ++x)
      if (x * x + y * y <= ri * ri + ri) set(cx + x, cy + y, c);
}

void Raster::polygon(const std::vector<glm::vec2>& pts, glm::u8vec3 c) {
  if (pts.size() < 3) return;
  float top = pts[0].y, bottom = top;
  for (const glm::vec2& p : pts) top = std::min(top, p.y), bottom = std::max(bottom, p.y);
  const int y0 = std::max(0, int(std::ceil(top - 0.5f))), y1 = std::min(h_ - 1, int(std::floor(bottom - 0.5f)));
  std::vector<float> xs;
  for (int y = y0; y <= y1; ++y) {
    const float sy = float(y) + 0.5f;
    xs.clear();
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
      const glm::vec2 a = pts[j], b = pts[i];
      if ((a.y <= sy) != (b.y <= sy)) xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t k = 0; k + 1 < xs.size(); k += 2) {
      const int x0 = std::max(0, int(std::ceil(xs[k] - 0.5f))), x1 = std::min(w_ - 1, int(std::floor(xs[k + 1] - 0.5f)));
      for (int x = x0; x <= x1; ++x) set(x, y, c);
    }
  }
}

void Raster::rect(glm::ivec2 lo, glm::ivec2 hi, glm::u8vec3 c) {
  for (int x = lo.x; x <= hi.x; ++x) set(x, lo.y, c), set(x, hi.y, c);
  for (int y = lo.y; y <= hi.y; ++y) set(lo.x, y, c), set(hi.x, y, c);
}

}  // namespace menu
