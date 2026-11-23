#include "gamedata/original.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace gamedata {

namespace fs = std::filesystem;

namespace {

bool sameNoCase(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::toupper(x) == std::toupper(y); });
}

// `dir`/`name` with the name matched ignoring case, as on the DOS file system the game came from.
fs::path findEntry(const fs::path& dir, const std::string& name) {
  std::error_code ec;
  if (fs::exists(dir / name, ec)) return dir / name;
  for (const auto& e : fs::directory_iterator(dir, ec))
    if (sameNoCase(e.path().filename().string(), name)) return e.path();
  return {};
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

bool OriginalData::open(const std::string& dir) {
  objs_.clear();
  offsets_.clear();
  palette_.clear();
  fs::path data = dir.empty() ? fs::path() : findEntry(dir, "3DDATA");
  fs::path objs = data.empty() ? fs::path() : findEntry(data, "3DOBJS.BIN");
  fs::path pal = data.empty() ? fs::path() : findEntry(data, "3DPAL.RGB");
  std::vector<uint8_t> palBytes;
  if (objs.empty() || pal.empty() || !readFile(objs, objs_) || !readFile(pal, palBytes)) {
    error_ = "no 3DDATA/3DOBJS.BIN and 3DPAL.RGB in '" + dir + "'";
    objs_.clear();
    return false;
  }
  for (size_t i = 0; i + 2 < palBytes.size(); i += 3)
    palette_.push_back(glm::vec3(palBytes[i], palBytes[i + 1], palBytes[i + 2]) / 255.0f);
  // The objects' textures, in the order of their table (each entry's offset, until the first
  // missing one).
  textures_.clear();
  std::vector<uint8_t> tex;
  if (fs::path t = findEntry(data, "OBJTEXT.BIN"); !t.empty() && readFile(t, tex)) {
    constexpr size_t kTexels = kTextureSize * kTextureSize;
    for (size_t e = 4; e + 12 <= tex.size(); e += 24) {
      uint32_t at;
      std::memcpy(&at, &tex[e + 8], 4);
      if (at == 0 || at + 12 + kTexels > tex.size()) break;
      for (size_t i = 0; i < kTexels; ++i) {
        const uint8_t ci = tex[at + 12 + i];
        const glm::vec3 c = ci < palette_.size() ? palette_[ci] : glm::vec3(1, 0, 1);
        textures_.insert(textures_.end(), {uint8_t(c.r * 255.0f + 0.5f), uint8_t(c.g * 255.0f + 0.5f), uint8_t(c.b * 255.0f + 0.5f), 255});
      }
    }
  }

  auto u32 = [&](size_t at) {
    uint32_t v;
    std::memcpy(&v, &objs_[at], 4);
    return v;
  };
  uint32_t n = objs_.size() >= 4 ? u32(0) : 0;
  if (n == 0 || n > 100000 || (n + 1) * 4 > objs_.size()) {
    error_ = objs.string() + ": not an object file";
    objs_.clear();
    return false;
  }
  offsets_.assign(n + 1, 0);
  for (uint32_t i = 1; i <= n; ++i) {
    offsets_[i] = u32(i * 4);
    if (offsets_[i] + 0xbc > objs_.size()) {
      error_ = objs.string() + ": damaged object table";
      objs_.clear();
      offsets_.clear();
      return false;
    }
  }
  return true;
}

std::string OriginalData::name(int index) const {
  if (index < 1 || index > count()) return {};
  const char* p = reinterpret_cast<const char*>(&objs_[offsets_[index] + 0x58]);
  // A few names start with stray control characters ("\r\tMi_24V_OPMount_9M114").
  std::string n(p, strnlen(p, 32));
  const size_t b = n.find_first_not_of(" \t\r\n");
  return b == std::string::npos ? std::string() : n.substr(b, n.find_last_not_of(" \t\r\n") - b + 1);
}

int OriginalData::find(const std::string& wanted) const {
  for (int i = 1; i <= count(); ++i)
    if (name(i) == wanted) return i;
  return -1;
}

Model OriginalData::model(int index, const std::vector<std::string>& skip) const {
  Model m;
  skipped_ = 0;
  if (index < 1 || index > count()) return m;
  m.name = name(index);
  gather(index, glm::ivec3(0), 0, m, skip);
  if (!m.polys.empty()) {
    m.lo = m.hi = m.polys[0].pts[0];
    for (const Polygon& p : m.polys)
      for (glm::vec3 v : p.pts) {
        m.lo = glm::min(m.lo, v);
        m.hi = glm::max(m.hi, v);
      }
  }
  liftDecals(m);
  return m;
}

// The original draws its polygons in the order of its BSP tree, so a window lying on a panel is
// drawn after it; with a depth buffer the two in one plane would fight (a speckle like a QR
// code). A polygon lying within a bigger one in the same plane (a window on the hull, the hull
// round a doorway on the doorway's dark fill) is lifted off it a little, outwards, once for
// each one it lies on.
void OriginalData::liftDecals(Model& m) {
  constexpr float kLift = 0.012f;  // m
  const glm::vec3 centre = 0.5f * (m.lo + m.hi);
  struct Plane {
    glm::vec3 n{0.0f}, lo, hi;
    float d = 0.0f, size = 0.0f;
  };
  std::vector<Plane> planes(m.polys.size());
  for (size_t k = 0; k < m.polys.size(); ++k) {
    const Polygon& p = m.polys[k];
    Plane& q = planes[k];
    // Newell's normal, the box, the plane's distance.
    glm::vec3 n(0.0f), c(0.0f);
    q.lo = q.hi = p.pts[0];
    for (size_t i = 0; i < p.pts.size(); ++i) {
      const glm::vec3 a = p.pts[i], b = p.pts[(i + 1) % p.pts.size()];
      n += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
      c += a, q.lo = glm::min(q.lo, a), q.hi = glm::max(q.hi, a);
    }
    c /= float(p.pts.size());
    const float len = glm::length(n);
    if (len < 1e-9f) continue;
    q.size = 0.5f * len;  // its area
    q.n = n / len;
    if (glm::dot(q.n, c - centre) < 0.0f) q.n = -q.n;  // outwards
    q.d = glm::dot(q.n, c);
  }
  std::vector<int> under(m.polys.size(), 0);
  for (size_t k = 0; k < planes.size(); ++k) {
    const Plane& p = planes[k];
    if (p.size <= 0.0f) continue;
    for (size_t j = 0; j < planes.size(); ++j) {
      const Plane& q = planes[j];
      if (j == k || q.size <= p.size * 1.001f || std::fabs(glm::dot(q.n, p.n)) < 0.999f) continue;
      if (std::fabs(glm::dot(q.n, 0.5f * (p.lo + p.hi)) - q.d) > 0.004f) continue;
      if (glm::all(glm::greaterThanEqual(p.lo, q.lo - 0.002f)) && glm::all(glm::lessThanEqual(p.hi, q.hi + 0.002f))) ++under[k];
    }
  }
  for (size_t k = 0; k < m.polys.size(); ++k)
    if (under[k] > 0)
      for (glm::vec3& v : m.polys[k].pts) v += planes[k].n * (kLift * float(std::min(under[k], 3)));
}

// `offset` in 1/16 vertex units, original axes.
void OriginalData::gather(int index, glm::ivec3 offset, int depth, Model& out, const std::vector<std::string>& skip) const {
  const size_t base = offsets_[index];
  const size_t end = index < count() ? std::min<size_t>(offsets_[index + 1], objs_.size()) : objs_.size();
  if (base >= end) return;
  const uint8_t* o = &objs_[base];
  const size_t size = end - base;
  auto u16 = [&](size_t at) { return at + 2 <= size ? uint32_t(o[at] | o[at + 1] << 8) : 0u; };
  auto u32 = [&](size_t at) {
    uint32_t v = 0;
    if (at + 4 <= size) std::memcpy(&v, o + at, 4);
    return v;
  };
  uint32_t sec[5];
  for (int i = 0; i < 5; ++i) sec[i] = u32(0x30 + 4 * i);
  if (sec[1] >= size || sec[3] > sec[4] || sec[4] > size) return;

  // Vertices, converted to game axes and metres. Packed (the header's first u32 is 1: buildings,
  // scrub): x, y, z bytes in steps of 256 units, then x, y, z bytes in steps of 4, both + 32.
  std::vector<glm::vec3> verts;
  const uint32_t nv = u16(sec[1]);
  const bool packed = u32(0) == 1;
  for (uint32_t i = 0; i < nv && sec[1] + 8 + 6 * i <= size; ++i) {
    const uint8_t* v = o + sec[1] + 2 + 6 * i;
    glm::vec3 p = packed ? glm::vec3((v[0] - 32) * 256 + (v[3] - 32) * 4, (v[1] - 32) * 256 + (v[4] - 32) * 4,
                                     (v[2] - 32) * 256 + (v[5] - 32) * 4)
                         : glm::vec3(int16_t(v[0] | v[1] << 8), int16_t(v[2] | v[3] << 8), int16_t(v[4] | v[5] << 8));
    p += glm::vec3(offset) / 16.0f;
    verts.push_back(glm::vec3(p.x, p.z, -p.y) * kMetresPerUnit);
  }

  // Display list.
  const uint8_t* d = o + sec[3];
  const size_t len = sec[4] - sec[3];
  size_t pos = 0;
  // After the first of several variants (cmd 5), where the drawing goes on past the others.
  std::vector<std::pair<size_t, size_t>> jumps;
  while (pos < len) {
    if (!jumps.empty() && pos >= jumps.back().first) {
      pos = std::max(pos, jumps.back().second);
      jumps.pop_back();
      continue;
    }
    uint8_t c = d[pos];
    if (c == 2) {  // BSP node: plane data, then the two halves follow inline
      pos += pos + 1 < len ? 2 + d[pos + 1] + 2 : len;
      continue;
    }
    if (c == 3) { pos += 9; continue; }    // node variants with a fixed size
    if (c == 4) { pos += 11; continue; }
    if (c == 5) {
      // Variants of a part (the MiG-27's wings swept and spread): k, 4 bytes, 0, the u16 size of
      // them all, then k u16s: each says where the variant before it ends (counted from the
      // byte after it); only the first is drawn.
      const size_t k = pos + 1 < len ? d[pos + 1] : 0;
      const size_t head = 9 + 2 * k;
      if (k == 0 || pos + head > len) break;
      auto at = [&](size_t field) { return field + 1 + size_t(d[field] | d[field + 1] << 8); };
      const size_t all = at(pos + 7), first = k > 1 ? at(pos + 11) : all;
      if (first < all && all < len) jumps.push_back({first + 1, all + 1});
      pos += head;
      continue;
    }
    if (c == 6) { pos += 2; continue; }    // draw a sub-object here (placed from the header instead)
    if (c == 7) { pos += 1; continue; }    // end of a branch
    if (c != 0 && c != 1) {
      ++skipped_;
      ++pos;
      continue;
    }
    int given = -1;  // prefix 01 gives the polygon's length in bytes
    if (c == 1) {
      if (pos + 1 >= len) break;
      given = d[pos + 1];
      pos += 2;
    } else {
      pos += 1;
    }
    if (pos + 3 > len) break;
    const uint8_t type = d[pos], col = d[pos + 2];
    size_t n = d[pos + 1];
    if (type == 0 && n == 0) break;  // padding at the end
    // Where the vertex indices start, how far apart, and the polygon's size. Textured polygons
    // (0x0f, 0x13) follow each index with its texture coordinates.
    size_t first, stride = 1, bytes;
    switch (type) {
      case 0x00: first = 3; bytes = 3 + n; n = 0; break;  // points (lights), not drawn
      case 0x01: case 0x02: case 0x04: case 0x05: first = 3; bytes = 3 + n; break;  // no face normal
      case 0x06: first = 3; bytes = 2 + n; n = n > 0 ? n - 1 : 0; break;  // the count includes the colour (rotor discs)
      case 0x07: first = 5; stride = 3; bytes = 5 + 3 * n; break;
      case 0x0c: first = 5; bytes = 5 + n; break;
      case 0x0f: first = 6; stride = 3; bytes = 6 + 3 * n; break;
      case 0x11: first = 4; stride = 2; bytes = 4 + 2 * n; break;  // with vertex normals
      case 0x12: case 0x15: first = 5; stride = 2; bytes = 5 + 2 * n; break;
      case 0x13: first = 6; stride = 4; bytes = 6 + 4 * n; break;
      default: first = 4; bytes = 4 + n; break;
    }
    if (given >= 0 && size_t(given) != bytes) {
      ++skipped_;
      bytes = given;
    }
    if (pos + bytes > len) break;
    // Light beams of the headlights lying on the ground in front of trucks, the grey box of
    // soldiers and guns (not shown in the game).
    bool beam = (type == 0x0d && col == 168) || (type == 0x02 && col == 82);
    Polygon poly;
    poly.color = col < palette_.size() ? palette_[col] : glm::vec3(1, 0, 1);
    poly.type = type;
    poly.colorIndex = col;
    poly.oneSided = type == 0x0b || type == 0x0c || type == 0x0d || type == 0x0f || type == 0x11 || type == 0x13;
    const bool textured = (type == 0x0f || type == 0x13) && (d[pos + 5] - 2) % 6 == 0 && (d[pos + 5] - 2) / 6 < textureCount();
    if (textured) poly.texture = (d[pos + 5] - 2) / 6;
    for (size_t k = 0; k < n && first + k * stride + stride - 1 < bytes; ++k) {
      const uint8_t* at = d + pos + first + k * stride;
      if (at[0] >= verts.size()) continue;
      poly.pts.push_back(verts[at[0]]);
      if (textured) poly.uv.push_back(type == 0x13 ? glm::vec2(at[2], at[3]) : glm::vec2(at[1], at[2]));
    }
    // 0x06: the translucent sectors of a turning rotor's disc, coloured from a cycling part of
    // the palette.
    if (type == 0x06) beam = true;
    // White paint on the ground (runway stripes, markings) is drawn, unlike the soldiers' box.
    if (type == 0x02 && col == 82 &&
        std::all_of(poly.pts.begin(), poly.pts.end(), [](glm::vec3 p) { return p.y == 0.0f; }))
      beam = false;
    if (poly.pts.size() >= 3 && !beam) out.polys.push_back(std::move(poly));
    pos += bytes;
  }

  // Sub-objects: count at 0xbc, then entries of (object - 1, x, y, z, ...) as int32.
  if (depth < 4 && sec[0] > 0xc0) {
    uint32_t cnt = u32(0xbc);
    if (cnt == 0 || cnt > 64) return;
    uint32_t entry = (sec[0] - 0xc0) / cnt;
    if (entry < 16) return;
    for (uint32_t k = 0; k < cnt; ++k) {
      size_t at = 0xc0 + k * entry;
      int sub = int(u32(at)) + 1;
      glm::ivec3 p{int32_t(u32(at + 4)), int32_t(u32(at + 8)), int32_t(u32(at + 12))};
      if (sub >= 1 && sub <= count() && sub != index && std::find(skip.begin(), skip.end(), name(sub)) == skip.end())
        gather(sub, offset + p, depth + 1, out, skip);
    }
  }
}

std::vector<OriginalData::Part> OriginalData::parts(int index) const {
  std::vector<Part> out;
  if (index < 1 || index > count()) return out;
  const size_t base = offsets_[index];
  auto u32 = [&](size_t at) {
    uint32_t v = 0;
    if (base + at + 4 <= objs_.size()) std::memcpy(&v, &objs_[base + at], 4);
    return v;
  };
  const uint32_t sec0 = u32(0x30), cnt = u32(0xbc);
  if (sec0 <= 0xc0 || cnt == 0 || cnt > 64) return out;
  const uint32_t entry = (sec0 - 0xc0) / cnt;
  if (entry < 16) return out;
  for (uint32_t k = 0; k < cnt; ++k) {
    const size_t at = 0xc0 + k * entry;
    const glm::vec3 p = glm::vec3(int32_t(u32(at + 4)), int32_t(u32(at + 8)), int32_t(u32(at + 12))) / 16.0f;
    out.push_back({int(u32(at)) + 1, glm::vec3(p.x, p.z, -p.y) * kMetresPerUnit});
  }
  return out;
}

int OriginalData::cleanest(int index) const {
  const std::string n = name(index);
  for (int k = index; k <= count() && k < index + 3 && name(k) == n; ++k) {
    Model m = model(k);
    if (!m.polys.empty() && skipped_ * 4 <= int(m.polys.size())) return k;
  }
  return index;
}

std::string findOriginalFile(const std::string& dir, const std::string& relative) {
  fs::path p = dir;
  size_t start = 0;
  while (start <= relative.size() && !p.empty()) {
    size_t slash = relative.find('/', start);
    std::string part = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    p = findEntry(p, part);
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return p.string();
}

std::string findOriginalDir(const std::string& configured) {
  auto isGame = [](const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec) && !findEntry(p, "3DDATA").empty();
  };
  std::vector<fs::path> dirs;
  // The home directory (Windows: the user's profile).
  const char* home = std::getenv("HOME");
#ifdef _WIN32
  if (!home || !*home) home = std::getenv("USERPROFILE");
#endif
  if (!configured.empty()) {
    std::string c = configured;
    if (c[0] == '~' && home) c = home + c.substr(1);
    dirs.push_back(c);
  }
  if (home) {
    for (const char* d : {"dosbox/c/DI/HIND", "dosbox/c/HIND", "dosbox/DI/HIND", "dos/DI/HIND", "DI/HIND", "HIND"})
      dirs.push_back(fs::path(home) / d);
  }
#ifdef _WIN32
  // Where a DOS game lies on a Windows machine, or DOSBox's C: drive there.
  for (const char* d : {"C:/DI/HIND", "C:/HIND", "C:/DOSBox/DI/HIND", "C:/DOSBox/c/DI/HIND", "C:/Games/DI/HIND", "C:/Games/HIND"})
    dirs.push_back(d);
#endif
  dirs.push_back("HIND");
  for (const fs::path& d : dirs)
    if (isGame(d)) return d.string();
  return {};
}

}  // namespace gamedata
