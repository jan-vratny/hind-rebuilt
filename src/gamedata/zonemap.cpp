#include "gamedata/zonemap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include "gamedata/original.hpp"
#include "gamedata/zone.hpp"

namespace gamedata {

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

uint32_t u32(const std::vector<uint8_t>& d, size_t at) {
  uint32_t v = 0;
  if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
  return v;
}

uint16_t u16(const std::vector<uint8_t>& d, size_t at) { return at + 2 <= d.size() ? uint16_t(d[at] | d[at + 1] << 8) : 0; }

// One cell's feature at `at` .. `end` of FEATURES.BIN, its corner `corner` (ft).
void readFeature(const std::vector<uint8_t>& d, size_t at, size_t end, glm::vec2 corner, ZoneMap& out,
                 std::vector<uint32_t>& cell) {
  if (at + 12 > end) return;
  const size_t verts = at + u32(d, at), areas = at + u32(d, at + 4), lines = at + u32(d, at + 8);
  std::vector<glm::vec2> v;
  size_t p = verts + 2;
  for (int i = 0, n = u16(d, verts); i < n && p + 3 <= end; ++i) {
    const uint8_t t = d[p];
    glm::vec2 q{float((d[p + 1] & 0x7f) * 64), float((d[p + 2] & 0x7f) * 64)};
    if ((t & 4) && p + 5 <= end) q += glm::vec2(float(d[p + 3]) - 32.0f, float(d[p + 4]) - 32.0f);
    v.push_back((corner + q * 16.0f) * kFeet);
    p += 3 + ((t & 4) ? 2 : 0) + ((t & 8) ? 2 : 0);
  }
  for (const size_t list : {areas, lines}) {
    p = list + 2;
    for (int i = 0, n = u16(d, list); i < n && p + 4 <= end; ++i) {
      const uint8_t form = d[p], kind = d[p + 1], count = d[p + 2];
      ZoneMap::Shape s;
      s.kind = ZoneMap::Kind(kind);
      s.area = list == areas;
      s.colour = d[p + 3];
      bool ok = p + 4 + count <= end;
      for (int k = 0; ok && k < count; ++k) {
        const uint8_t index = d[p + 4 + size_t(k)];
        if (index < v.size()) s.pts.push_back(v[index]);
        else ok = false;
      }
      p += 4 + count;
      // An area's edge again as a line is not drawn.
      if (!ok || s.pts.size() < 2 || (!s.area && form == 2)) continue;
      cell.push_back(uint32_t(out.shapes.size()));
      out.shapes.push_back(std::move(s));
    }
  }
}

}  // namespace

bool loadZoneMap(const std::string& gameDir, const std::string& zoneDir, ZoneMap& out, std::string& error) {
  const std::string dir = findOriginalFile(gameDir, zoneDir);
  if (dir.empty()) {
    error = "no " + zoneDir + " in '" + gameDir + "'";
    return false;
  }
  const std::vector<uint8_t> grid = readFile(findOriginalFile(dir, "GRIDPOST.BIN"));
  const std::vector<uint8_t> pal = readFile(findOriginalFile(dir, "3DPAL.RGB"));
  const int n = 257;
  if (grid.size() != size_t(n) * n * 8 || pal.size() < 208 * 3) {
    error = zoneDir + ": no GRIDPOST.BIN or 3DPAL.RGB";
    return false;
  }
  out = {};
  out.size = n;
  out.spacing = 4096.0f * kFeet;
  out.heightFt.resize(size_t(n) * n);
  for (auto& c : out.colour) c.resize(out.heightFt.size());
  for (size_t k = 0; k < out.heightFt.size(); ++k) {
    out.heightFt[k] = float(grid[k * 8] | grid[k * 8 + 1] << 8);
    out.colour[0][k] = grid[k * 8 + 3];
    out.colour[1][k] = grid[k * 8 + 4];
  }
  for (int i = 0; i < 16; ++i) out.palette[size_t(i)] = {pal[size_t(192 + i) * 3], pal[size_t(192 + i) * 3 + 1], pal[size_t(192 + i) * 3 + 2]};

  constexpr int C = ZoneMap::kCells;
  out.cells.assign(size_t(C) * C, {});
  const std::vector<uint8_t> feat = readFile(findOriginalFile(dir, "FEATURES.BIN"));
  const std::vector<uint8_t> lyr = readFile(findOriginalFile(dir, "FEATURE1.LYR"));
  const uint32_t count = u32(feat, 0);
  if (lyr.size() >= size_t(C) * C * 4 && feat.size() >= 4 + size_t(count) * 4)
    for (int c = 0; c < C * C; ++c) {
      const uint32_t k = u32(lyr, size_t(c) * 4);
      if (k == 0 || k > count) continue;
      const size_t at = u32(feat, 4 * size_t(k)), end = k < count ? u32(feat, 4 * size_t(k) + 4) : feat.size();
      if (at >= end || end > feat.size()) continue;
      readFeature(feat, at, end, glm::vec2(float(c / C), float(c % C)) * 16384.0f, out, out.cells[size_t(c)]);
    }

  // Contours every 250 ft through the triangles of the posts.
  const float cell = out.cellSize();
  for (int i = 0; i + 1 < n; ++i)
    for (int j = 0; j + 1 < n; ++j) {
      const bool even = (i + j) % 2 == 0;
      const glm::ivec2 tris[2][3] = {
          {{i, j}, {i + 1, j}, even ? glm::ivec2(i + 1, j + 1) : glm::ivec2(i, j + 1)},
          {even ? glm::ivec2(i, j) : glm::ivec2(i + 1, j), {i + 1, j + 1}, {i, j + 1}}};
      for (const auto& t : tris) {
        float lo = 1e9f, hi = -1e9f;
        for (const glm::ivec2& p : t) lo = std::min(lo, out.height(p.x, p.y)), hi = std::max(hi, out.height(p.x, p.y));
        for (float level = std::ceil(lo / ZoneMap::kContourFt) * ZoneMap::kContourFt; level < hi; level += ZoneMap::kContourFt) {
          if (level <= 0.0f) continue;
          ZoneMap::Shape s;
          s.kind = ZoneMap::Contour;
          s.level = level;
          for (int e = 0; e < 3; ++e) {
            const glm::ivec2 a = t[e], b = t[(e + 1) % 3];
            const float ha = out.height(a.x, a.y), hb = out.height(b.x, b.y);
            if ((ha < level) == (hb < level)) continue;
            const float f = (level - ha) / (hb - ha);
            s.pts.push_back(glm::mix(glm::vec2(a), glm::vec2(b), f) * out.spacing);
          }
          if (s.pts.size() != 2) continue;
          const glm::vec2 mid = (s.pts[0] + s.pts[1]) * 0.5f / cell;
          const int cx = std::clamp(int(mid.x), 0, C - 1), cy = std::clamp(int(mid.y), 0, C - 1);
          out.cells[size_t(cx * C + cy)].push_back(uint32_t(out.shapes.size()));
          out.shapes.push_back(std::move(s));
        }
      }
    }
  return true;
}

}  // namespace gamedata
