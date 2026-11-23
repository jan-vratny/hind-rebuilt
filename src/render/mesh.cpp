#include "render/mesh.hpp"

#include <cmath>

namespace render {

void MeshBuilder::face(const std::vector<glm::vec3>& pts, glm::vec3 color, bool oneSided, const glm::mat4& m,
                       const std::vector<glm::vec2>& uv, int texture) {
  if (texture < 0 || texture >= kAtlasTiles * kAtlasTiles || uv.size() != pts.size()) return face(pts, color, oneSided, m);
  // The texel centres of the corners, in the atlas.
  const glm::vec2 tile(float(texture % kAtlasTiles * kAtlasTile), float(texture / kAtlasTiles * kAtlasTile));
  auto at = [&](size_t i) {
    return Vertex{glm::vec3(m * glm::vec4(pts[i], 1.0f)), glm::vec3(0.0f), glm::vec4(1.0f),
                  glm::vec3((tile + glm::clamp(uv[i], 0.0f, float(kAtlasTile - 1)) + 0.5f) / float(kAtlasSize), 0.0f)};
  };
  auto tri = [&](Vertex a, Vertex b, Vertex c) {
    glm::vec3 n = glm::cross(b.pos - a.pos, c.pos - a.pos);
    const float len = glm::length(n);
    n = len > 1e-9f ? n / len : glm::vec3(0, 1, 0);
    a.normal = b.normal = c.normal = n;
    verts.insert(verts.end(), {a, b, c});
  };
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    tri(at(0), at(i + 1), at(i));
    if (!oneSided) tri(at(0), at(i), at(i + 1));
  }
}

void MeshBuilder::face(const std::vector<glm::vec3>& pts, glm::vec3 color, bool oneSided, const glm::mat4& m) {
  auto at = [&](size_t i) { return glm::vec3(m * glm::vec4(pts[i], 1.0f)); };
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    tri(at(0), at(i + 1), at(i), color);
    if (!oneSided) tri(at(0), at(i), at(i + 1), color);
  }
}

void MeshBuilder::tri(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color) {
  glm::vec3 n = glm::cross(b - a, c - a);
  float len = glm::length(n);
  n = len > 1e-9f ? n / len : glm::vec3(0, 1, 0);
  verts.push_back({a, n, color});
  verts.push_back({b, n, color});
  verts.push_back({c, n, color});
}

void MeshBuilder::quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color) {
  tri(a, b, c, color);
  tri(a, c, d, color);
}

void MeshBuilder::hexa(const glm::vec3 (&c)[8], glm::vec3 color) {
  quad(c[0], c[1], c[2], c[3], color);
  quad(c[4], c[7], c[6], c[5], color);
  for (int i = 0; i < 4; ++i) {
    int j = (i + 1) % 4;
    quad(c[i], c[i + 4], c[j + 4], c[j], color);
  }
}

void MeshBuilder::box(const glm::mat4& m, glm::vec3 lo, glm::vec3 hi, glm::vec3 color) {
  auto p = [&](float x, float y, float z) { return glm::vec3(m * glm::vec4(x, y, z, 1.0f)); };
  const glm::vec3 c[8] = {
      p(lo.x, lo.y, lo.z), p(hi.x, lo.y, lo.z), p(hi.x, hi.y, lo.z), p(lo.x, hi.y, lo.z),
      p(lo.x, lo.y, hi.z), p(hi.x, lo.y, hi.z), p(hi.x, hi.y, hi.z), p(lo.x, hi.y, hi.z),
  };
  hexa(c, color);
}

void MeshBuilder::pyramid(glm::vec3 base, float hw, float h, float rot, glm::vec3 color) {
  float cs = std::cos(rot), sn = std::sin(rot);
  glm::vec3 c[4];
  const float sx[4] = {-1, 1, 1, -1}, sz[4] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; ++i) {
    float x = sx[i] * hw, z = sz[i] * hw;
    c[i] = base + glm::vec3(x * cs - z * sn, 0.0f, x * sn + z * cs);
  }
  glm::vec3 top = base + glm::vec3(0, h, 0);
  for (int i = 0; i < 4; ++i) {
    // Alternate shades make the facets read even under flat light.
    glm::vec3 shade = color * (i % 2 ? 0.85f : 1.0f);
    tri(c[i], c[(i + 1) % 4], top, shade);
  }
}

void MeshBuilder::house(glm::vec3 base, float w, float d, float wallH, float roofH, float rot, glm::vec3 wall,
                        glm::vec3 roof) {
  float cs = std::cos(rot), sn = std::sin(rot);
  auto p = [&](float x, float y, float z) { return base + glm::vec3(x * cs - z * sn, y, x * sn + z * cs); };
  float hx = w * 0.5f, hz = d * 0.5f;
  const glm::vec3 c[8] = {
      p(-hx, -1.0f, -hz), p(hx, -1.0f, -hz), p(hx, wallH, -hz), p(-hx, wallH, -hz),
      p(-hx, -1.0f, hz),  p(hx, -1.0f, hz),  p(hx, wallH, hz),  p(-hx, wallH, hz),
  };
  hexa(c, wall);
  glm::vec3 r0 = p(0, wallH + roofH, -hz - 0.3f), r1 = p(0, wallH + roofH, hz + 0.3f);
  glm::vec3 e[4] = {p(-hx - 0.4f, wallH - 0.3f, -hz - 0.3f), p(hx + 0.4f, wallH - 0.3f, -hz - 0.3f),
                    p(hx + 0.4f, wallH - 0.3f, hz + 0.3f), p(-hx - 0.4f, wallH - 0.3f, hz + 0.3f)};
  quad(e[0], r0, r1, e[3], roof);
  quad(e[1], e[2], r1, r0, roof * 0.8f);
  tri(c[3], c[2], p(0, wallH + roofH, -hz), wall);
  tri(c[6], c[7], p(0, wallH + roofH, hz), wall);
}

namespace {

glm::vec3 xf(const glm::mat4& m, glm::vec3 v) { return glm::vec3(m * glm::vec4(v, 1.0f)); }

glm::vec3 centroid(const std::vector<glm::vec3>& pts) {
  glm::vec3 c{0.0f};
  for (const glm::vec3& p : pts) c += p;
  return c / float(pts.size());
}

}  // namespace

void MeshBuilder::loft(const glm::mat4& m, const std::vector<std::vector<glm::vec3>>& rings,
                       const FacetColor& color, bool capFront, bool capBack) {
  for (size_t r = 0; r + 1 < rings.size(); ++r) {
    const auto &a = rings[r], &b = rings[r + 1];
    for (size_t i = 0; i < a.size(); ++i) {
      size_t j = (i + 1) % a.size();
      glm::vec3 col = color(int(r), int(i), (a[i] + a[j] + b[i] + b[j]) * 0.25f);
      if (col.r < 0.0f) continue;
      quad(xf(m, a[i]), xf(m, a[j]), xf(m, b[j]), xf(m, b[i]), col);
    }
  }
  auto cap = [&](const std::vector<glm::vec3>& ring, int r) {
    glm::vec3 c = centroid(ring);
    for (size_t i = 0; i < ring.size(); ++i) {
      size_t j = (i + 1) % ring.size();
      glm::vec3 col = color(r, int(i), (c + ring[i] + ring[j]) / 3.0f);
      if (col.r >= 0.0f) tri(xf(m, c), xf(m, ring[i]), xf(m, ring[j]), col);
    }
  };
  if (capFront) cap(rings.front(), 0);
  if (capBack) cap(rings.back(), int(rings.size()) - 1);
}

void MeshBuilder::cylinder(const glm::mat4& m, glm::vec3 a, glm::vec3 b, float ra, float rb, int sides,
                           glm::vec3 color, glm::vec3 capColor) {
  glm::vec3 d = glm::normalize(b - a);
  glm::vec3 u = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 s1 = glm::normalize(glm::cross(d, u)), s2 = glm::cross(d, s1);
  std::vector<glm::vec3> ca, cb;
  for (int i = 0; i < sides; ++i) {
    float t = (i + 0.5f) * 6.2831853f / sides;
    glm::vec3 dir = s1 * std::cos(t) + s2 * std::sin(t);
    ca.push_back(xf(m, a + dir * ra));
    cb.push_back(xf(m, b + dir * rb));
  }
  glm::vec3 pa = xf(m, a), pb = xf(m, b);
  for (int i = 0; i < sides; ++i) {
    int j = (i + 1) % sides;
    quad(ca[i], ca[j], cb[j], cb[i], color);
    if (ra > 0.0f) tri(pa, ca[j], ca[i], capColor);
    if (rb > 0.0f) tri(pb, cb[i], cb[j], capColor);
  }
}

void MeshBuilder::slab(const glm::mat4& m, const std::vector<glm::vec3>& outline, glm::vec3 thick,
                       glm::vec3 color) {
  glm::vec3 h = thick * 0.5f, c = centroid(outline);
  size_t n = outline.size();
  for (size_t i = 0; i < n; ++i) {
    size_t j = (i + 1) % n;
    tri(xf(m, c + h), xf(m, outline[i] + h), xf(m, outline[j] + h), color);
    tri(xf(m, c - h), xf(m, outline[j] - h), xf(m, outline[i] - h), color);
    quad(xf(m, outline[i] - h), xf(m, outline[j] - h), xf(m, outline[j] + h), xf(m, outline[i] + h), color);
  }
}

}  // namespace render
