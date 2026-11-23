#include "gamedata/zone.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include "gamedata/original.hpp"

namespace gamedata {

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

uint32_t u32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

// A palette file's colours, 0..1 (VGA files with 6-bit components scaled up).
std::vector<glm::vec3> paletteColours(const std::vector<uint8_t>& d) {
  const bool sixBit = !d.empty() && *std::max_element(d.begin(), d.end()) < 64;
  std::vector<glm::vec3> out;
  for (size_t i = 0; i + 2 < d.size(); i += 3)
    out.push_back(glm::vec3(d[i], d[i + 1], d[i + 2]) / (sixBit ? 63.0f : 255.0f));
  return out;
}

glm::vec3 toHsv(glm::vec3 c) {
  const float hi = std::max({c.r, c.g, c.b}), lo = std::min({c.r, c.g, c.b}), d = hi - lo;
  float h = 0.0f;
  if (d > 0.0f) h = hi == c.r ? std::fmod((c.g - c.b) / d + 6.0f, 6.0f) : hi == c.g ? (c.b - c.r) / d + 2.0f : (c.r - c.g) / d + 4.0f;
  return {h / 6.0f, hi > 0.0f ? d / hi : 0.0f, hi};
}

glm::vec3 fromHsv(glm::vec3 c) {
  const float h = std::fmod(c.x + 1.0f, 1.0f) * 6.0f, v = c.z, lo = v * (1.0f - c.y);
  const float f = h - std::floor(h), down = v - (v - lo) * f, up = lo + (v - lo) * f;
  switch (int(h) % 6) {
    case 0: return {v, up, lo};
    case 1: return {down, v, lo};
    case 2: return {lo, v, up};
    case 3: return {lo, down, v};
    case 4: return {up, lo, v};
    default: return {v, lo, down};
  }
}

// The ground's tint of a zone. The original's palette (3DPAL.RGB, the same for every zone) has
// the dusty tan of Afghanistan in its two ground ramps; each zone's FOG.RGB gives the colour
// of its ground in the distance, a group of four from the ground to the haze for the light
// ramp (entry 4) and the dark one (entry 8), and the game turns its ramps that way: Saratov,
// Kazakhstan and Korea olive green. Here the hue, saturation and value of the default FOG.RGB's
// ground (3DDATA, Afghanistan's tan) to the zone's.
struct GroundTint {
  glm::vec3 shift[2]{glm::vec3(0.0f), glm::vec3(0.0f)};  // hue added, saturation and value scaled
  bool active[2]{false, false};
  // The ground of posts with no colour of their own (255: the texture's only), as the zone's
  // FOG.RGB gives it.
  glm::vec3 ground{0.6f, 0.5f, 0.35f};

  glm::vec3 apply(int ramp, glm::vec3 c) const {
    if (!active[ramp]) return c;
    glm::vec3 h = toHsv(c);
    h.x += shift[ramp].x;
    h.y = std::min(1.0f, h.y * shift[ramp].y);
    h.z = std::min(1.0f, h.z * shift[ramp].z);
    return fromHsv(h);
  }
};

GroundTint groundTint(const std::vector<uint8_t>& defaultFog, const std::vector<uint8_t>& zoneFog) {
  GroundTint t;
  const std::vector<glm::vec3> from = paletteColours(defaultFog), to = paletteColours(zoneFog);
  if (to.size() >= 5) t.ground = to[4];
  if (from.size() < 9 || to.size() < 9) return t;
  for (int r = 0; r < 2; ++r) {
    const glm::vec3 a = toHsv(from[size_t(4 + 4 * r)]), b = toHsv(to[size_t(4 + 4 * r)]);
    if (glm::distance(from[size_t(4 + 4 * r)], to[size_t(4 + 4 * r)]) < 0.08f || a.y <= 0.0f || a.z <= 0.0f) continue;
    float dh = b.x - a.x;
    if (dh > 0.5f) dh -= 1.0f;
    if (dh < -0.5f) dh += 1.0f;
    t.shift[r] = {dh, b.y / a.y, b.z / a.z};
    t.active[r] = true;
  }
  return t;
}

// The colour ramps of the zone palettes: each a hue from dark to light. The posts' colours have
// the shading of the slopes in them; the renderer shades the ground itself, so they are pulled
// halfway towards the middle of their ramp. The two tan ramps take the zone's ground tint: the
// light one (99 .. 114, FOG.RGB entry 4) and the dark one (83 .. 98, entry 8).
glm::vec3 postColour(const std::vector<uint8_t>& pal, int index, const GroundTint& tint) {
  auto rgb = [&](int i) {
    if (i < 0 || size_t(i) * 3 + 2 >= pal.size()) return tint.ground;
    return glm::vec3(pal[size_t(i) * 3], pal[size_t(i) * 3 + 1], pal[size_t(i) * 3 + 2]) / 255.0f;
  };
  const int ramps[][2] = {{99, 114}, {115, 126}, {127, 134}, {135, 138}};
  glm::vec3 c = rgb(index);
  for (const auto& r : ramps)
    if (index >= r[0] && index <= r[1]) c = glm::mix(rgb(index), rgb((r[0] + r[1] + 1) / 2), 0.5f);
  if (index >= 99 && index <= 114) return tint.apply(0, c);
  if (index >= 83 && index <= 98) return tint.apply(1, c);
  return c;
}

}  // namespace

const ZoneAirfield* Zone::airfield(const std::string& wanted) const {
  for (const ZoneAirfield& a : airfields)
    if (a.name == wanted) return &a;
  return nullptr;
}

world::HeightMap Zone::heightMap(glm::vec2 centre, float base) const {
  // Rows of the height map run from the north edge southwards.
  world::HeightMap m;
  m.size = size;
  m.spacing = spacing;
  m.origin = {-centre.x, -(float(size - 1) * spacing - centre.y)};
  m.height.resize(size_t(size) * size_t(size));
  m.colour.resize(m.height.size());
  for (int x = 0; x < size; ++x)
    for (int y = 0; y < size; ++y) {
      size_t from = size_t(x) * size_t(size) + size_t(y);
      size_t to = size_t(size - 1 - y) * size_t(size) + size_t(x);
      m.height[to] = height[from] + base;
      m.colour[to] = colour[from];
    }
  return m;
}

std::vector<world::Runway> zoneRunways(const Zone& zone, const OriginalData& original, glm::vec2 centre) {
  // The markings within reach, each a point on a runway's centre line and its direction (the
  // model's, east for "_EW" and the 09 and 27 numbers, otherwise north; turned by the heading).
  struct Mark {
    glm::vec2 at, dir;
    int group;
  };
  std::vector<Mark> marks;
  for (const ZoneObject& o : zone.objects) {
    if (glm::distance(o.pos, centre) > 5000.0f) continue;
    const std::string n = original.name(o.object);
    if (n.rfind("Runway_", 0) != 0) continue;
    const bool east = n.find("_EW") != std::string::npos || n.rfind("Runway_09", 0) == 0 || n.rfind("Runway_27", 0) == 0;
    const float a = (east ? 1.5707963f : 0.0f) + o.heading;
    glm::vec2 d{std::sin(a), std::cos(a)};
    if (d.y < -1e-3f || (std::fabs(d.y) <= 1e-3f && d.x < 0.0f)) d = -d;
    marks.push_back({o.pos, d, int(marks.size())});
  }
  // Those on one line are one runway.
  auto same = [](const Mark& p, const Mark& q) {
    const glm::vec2 off = q.at - p.at;
    return std::fabs(p.dir.x * q.dir.y - p.dir.y * q.dir.x) < 0.03f && std::fabs(p.dir.x * off.y - p.dir.y * off.x) < 10.0f;
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (Mark& p : marks)
      for (Mark& q : marks)
        if (p.group != q.group && same(p, q)) p.group = q.group = std::min(p.group, q.group), changed = true;
  }
  std::vector<world::Runway> out;
  for (size_t g = 0; g < marks.size(); ++g) {
    float lo = 1e9f, hi = -1e9f;
    int count = 0;
    const Mark* first = nullptr;
    for (const Mark& m : marks)
      if (m.group == int(g)) {
        if (!first) first = &m;
        const float t = glm::dot(m.at - first->at, first->dir);
        lo = std::min(lo, t), hi = std::max(hi, t);
        ++count;
      }
    if (count < 2) continue;
    // From the numbers' far edges.
    auto world = [&](float t) {
      const glm::vec2 p = first->at + first->dir * t;
      return glm::vec2(p.x - centre.x, centre.y - p.y);
    };
    out.push_back({world(lo - 30.0f), world(hi + 30.0f), 50.0f});
  }
  return out;
}

std::vector<ZoneAirfield> airfieldsFromMarkings(const Zone& zone, const OriginalData& original) {
  // The markings, gathered into clusters of those within 3 km of one another.
  std::vector<glm::vec2> marks;
  for (const ZoneObject& o : zone.objects)
    if (original.name(o.object).rfind("Runway_", 0) == 0) marks.push_back(o.pos);
  std::vector<int> group(marks.size());
  for (size_t i = 0; i < marks.size(); ++i) group[i] = int(i);
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < marks.size(); ++i)
      for (size_t j = 0; j < marks.size(); ++j)
        if (group[i] != group[j] && glm::distance(marks[i], marks[j]) < 3000.0f)
          group[i] = group[j] = std::min(group[i], group[j]), changed = true;
  }
  struct Cluster {
    glm::vec2 sum{0.0f};
    int count = 0;
  };
  std::vector<Cluster> clusters(marks.size());
  for (size_t i = 0; i < marks.size(); ++i) clusters[size_t(group[i])].sum += marks[i], ++clusters[size_t(group[i])].count;
  // An airfield has more than a couple of markings (two numbers and the stripes between).
  std::erase_if(clusters, [](const Cluster& c) { return c.count < 4; });
  std::stable_sort(clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) { return a.count > b.count; });
  std::vector<ZoneAirfield> out;
  for (const Cluster& c : clusters) out.push_back({"AIRFIELD " + std::to_string(out.size() + 1), c.sum / float(c.count)});
  return out;
}

bool loadZone(const std::string& gameDir, const std::string& zoneDir, Zone& out, std::string& error) {
  const std::string dir = findOriginalFile(gameDir, zoneDir);
  if (dir.empty()) {
    error = "no " + zoneDir + " in '" + gameDir + "'";
    return false;
  }
  std::vector<uint8_t> grid = readFile(findOriginalFile(dir, "GRIDPOST.BIN"));
  std::vector<uint8_t> pal = readFile(findOriginalFile(dir, "3DPAL.RGB"));
  const GroundTint tint = groundTint(readFile(findOriginalFile(gameDir, "3DDATA/FOG.RGB")), readFile(findOriginalFile(dir, "FOG.RGB")));
  const int n = 257;
  if (grid.size() != size_t(n) * n * 8) {
    error = zoneDir + "/GRIDPOST.BIN: not a 257 x 257 grid";
    return false;
  }
  out = {};
  out.name = zoneDir;
  out.size = n;
  out.spacing = 4096.0f * kFeet;
  out.height.resize(size_t(n) * n);
  out.colour.resize(size_t(n) * n);
  for (size_t k = 0; k < out.height.size(); ++k) {
    const uint8_t* p = &grid[k * 8];
    out.height[k] = float(p[0] | p[1] << 8) * kFeet;
    out.colour[k] = postColour(pal, p[3], tint);
  }
  std::vector<uint8_t> af = readFile(findOriginalFile(dir, "AIRFIELD.BIN"));
  if (af.size() >= 4) {
    uint32_t count = u32(af.data());
    for (uint32_t i = 0; i < count && 4 + 32 * (i + 1) <= af.size(); ++i) {
      const uint8_t* r = &af[4 + 32 * i];
      const char* name = reinterpret_cast<const char*>(r);
      ZoneAirfield a;
      a.name = std::string(name, strnlen(name, 12));
      while (!a.name.empty() && a.name.back() == ' ') a.name.pop_back();
      a.pos = glm::vec2(float(u32(r + 24)), float(u32(r + 28))) * kZoneUnit;
      out.airfields.push_back(a);
    }
  }
  std::vector<uint8_t> objs = readFile(findOriginalFile(dir, "GNDOBJS.BIN"));
  auto u16 = [&](size_t at) { return at + 2 <= objs.size() ? uint16_t(objs[at] | objs[at + 1] << 8) : uint16_t(0); };
  constexpr int kCells = 64;
  constexpr float kCell = 16384.0f;  // ft
  if (objs.size() >= size_t(kCells) * kCells * 4)
    for (int c = 0; c < kCells * kCells; ++c) {
      const size_t at = u32(&objs[size_t(c) * 4]);
      if (!at || at + 2 > objs.size()) continue;
      const int count = u16(at);
      for (int k = 0; k < count && at + 2 + 12 * size_t(k + 1) <= objs.size(); ++k) {
        const size_t r = at + 2 + 12 * size_t(k);
        ZoneObject o;
        o.object = u16(r) + 1;
        o.pos = glm::vec2(float(c / kCells) * kCell + float(u16(r + 2)), float(c % kCells) * kCell + float(u16(r + 4))) * kFeet;
        o.heading = float(u16(r + 6)) / 65536.0f * 6.2831853f;
        out.objects.push_back(o);
      }
    }
  return true;
}

}  // namespace gamedata
