#include "world/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace world {

uint32_t hash2(int x, int z, uint32_t seed) {
  uint32_t h = seed * 0x9E3779B9u;
  h ^= uint32_t(x) * 0x85EBCA6Bu;
  h = (h << 13) | (h >> 19);
  h ^= uint32_t(z) * 0xC2B2AE35u;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return h;
}

namespace {

float smooth5(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float smoothstep(float a, float b, float x) {
  float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Value noise in [-1, 1].
float valueNoise(float x, float z, uint32_t seed) {
  float fx = std::floor(x), fz = std::floor(z);
  int ix = int(fx), iz = int(fz);
  float tx = smooth5(x - fx), tz = smooth5(z - fz);
  auto v = [&](int dx, int dz) { return hashUnit(hash2(ix + dx, iz + dz, seed)) * 2.0f - 1.0f; };
  float a = v(0, 0) + (v(1, 0) - v(0, 0)) * tx;
  float b = v(0, 1) + (v(1, 1) - v(0, 1)) * tx;
  return a + (b - a) * tz;
}

float fbm(float x, float z, int octaves, uint32_t seed) {
  float sum = 0.0f, amp = 0.5f;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * valueNoise(x, z, seed + uint32_t(i) * 101u);
    x = x * 2.03f + 17.1f;
    z = z * 2.03f - 9.7f;
    amp *= 0.5f;
  }
  return sum;
}

// Ridged noise in [0, 1], sharp crests.
float ridged(float x, float z, int octaves, uint32_t seed) {
  float sum = 0.0f, amp = 0.5f, norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    float n = 1.0f - std::fabs(valueNoise(x, z, seed + uint32_t(i) * 131u));
    sum += amp * n * n;
    norm += amp;
    x = x * 2.1f + 3.3f;
    z = z * 2.1f - 5.1f;
    amp *= 0.5f;
  }
  return sum / norm;
}

}  // namespace

Terrain::Terrain(uint32_t seed) : seed_(seed) {
  airfield_.elevation = std::max(40.0f, naturalHeight(0.0f, 0.0f));
}

Terrain::Terrain(HeightMap map, std::vector<Runway> runways, std::vector<Pad> pads, uint32_t seed)
    : seed_(seed), map_(std::move(map)) {
  if (!runways.empty()) {
    // Level a circle round all of them.
    glm::vec2 lo = runways[0].a, hi = lo;
    for (const Runway& r : runways) lo = glm::min(lo, glm::min(r.a, r.b)), hi = glm::max(hi, glm::max(r.a, r.b));
    airfield_.center = (lo + hi) * 0.5f;
    airfield_.flatRadius = std::max(airfield_.flatRadius, glm::length(hi - lo) * 0.5f + 100.0f);
    airfield_.blendRadius = airfield_.flatRadius + 900.0f;
    airfield_.runways = std::move(runways);
    // The paved areas that lie on the levelled ground (the rest would hang over the hills).
    for (Pad& p : pads)
      if (std::all_of(p.outline.begin(), p.outline.end(), [&](glm::vec2 q) { return glm::distance(q, airfield_.center) < airfield_.flatRadius; }))
        airfield_.pads.push_back(std::move(p));
  }
  airfield_.elevation = mapHeight(airfield_.center.x, airfield_.center.y);
}

float Terrain::mapHeight(float x, float z) const {
  const int n = map_.size;
  float gx = std::clamp((x - map_.origin.x) / map_.spacing, 0.0f, float(n - 1) - 1e-3f);
  float gz = std::clamp((z - map_.origin.y) / map_.spacing, 0.0f, float(n - 1) - 1e-3f);
  int i = int(gx), j = int(gz);
  float fx = gx - float(i), fz = gz - float(j);
  auto h = [&](int a, int b) { return map_.height[size_t(b) * size_t(n) + size_t(a)]; };
  float top = h(i, j) + (h(i + 1, j) - h(i, j)) * fx;
  float bottom = h(i, j + 1) + (h(i + 1, j + 1) - h(i, j + 1)) * fx;
  return top + (bottom - top) * fz;
}

glm::vec3 Terrain::mapColour(float x, float z) const {
  const int n = map_.size;
  if (map_.colour.empty()) return {0.6f, 0.5f, 0.35f};
  int i = std::clamp(int(std::lround((x - map_.origin.x) / map_.spacing)), 0, n - 1);
  int j = std::clamp(int(std::lround((z - map_.origin.y) / map_.spacing)), 0, n - 1);
  return map_.colour[size_t(j) * size_t(n) + size_t(i)];
}

float Terrain::naturalHeight(float x, float z) const {
  if (mapped()) {
    // Between posts a kilometre apart, some rolling detail so that the valleys are not glass.
    return mapHeight(x, z) + 18.0f * (fbm(x / 700.0f, z / 700.0f, 3, seed_ + 5) - 0.5f);
  }
  float hills = 140.0f + 220.0f * fbm(x / 3500.0f, z / 3500.0f, 5, seed_);
  float mountainMask = smoothstep(0.02f, 0.35f, fbm(x / 16000.0f + 7.3f, z / 16000.0f - 3.1f, 3, seed_ + 11));
  float r = ridged(x / 5200.0f, z / 5200.0f, 5, seed_ + 23);
  float h = hills + mountainMask * r * r * 1600.0f;
  // Broad basins, some of which fall below sea level and become lakes.
  h -= 230.0f * smoothstep(0.15f, 0.5f, fbm(x / 9000.0f - 2.0f, z / 9000.0f + 5.0f, 3, seed_ + 37));
  return h;
}

float Terrain::rawHeight(float x, float z) const {
  float h = naturalHeight(x, z);
  float d = glm::length(glm::vec2(x, z) - airfield_.center);
  float t = smoothstep(airfield_.flatRadius, airfield_.blendRadius, d);
  return airfield_.elevation + (h - airfield_.elevation) * t;
}

float Terrain::vertexHeight(int ix, int iz) const {
  return std::max(0.0f, rawHeight(ix * kCell, iz * kCell));
}

float Terrain::groundHeight(float x, float z) const {
  float gx = x / kCell, gz = z / kCell;
  float fx0 = std::floor(gx), fz0 = std::floor(gz);
  int ix = int(fx0), iz = int(fz0);
  float fx = gx - fx0, fz = gz - fz0;
  float h00 = vertexHeight(ix, iz), h10 = vertexHeight(ix + 1, iz);
  float h01 = vertexHeight(ix, iz + 1), h11 = vertexHeight(ix + 1, iz + 1);
  // Cells are split along the (0,0)-(1,1) diagonal, same as the mesh.
  if (fx >= fz) return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
  return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
}

glm::vec3 Terrain::groundNormal(float x, float z) const {
  float gx = x / kCell, gz = z / kCell;
  float fx0 = std::floor(gx), fz0 = std::floor(gz);
  int ix = int(fx0), iz = int(fz0);
  float fx = gx - fx0, fz = gz - fz0;
  float h00 = vertexHeight(ix, iz), h10 = vertexHeight(ix + 1, iz);
  float h01 = vertexHeight(ix, iz + 1), h11 = vertexHeight(ix + 1, iz + 1);
  float dhdx, dhdz;
  if (fx >= fz) {
    dhdx = (h10 - h00) / kCell;
    dhdz = (h11 - h10) / kCell;
  } else {
    dhdx = (h11 - h01) / kCell;
    dhdz = (h01 - h00) / kCell;
  }
  return glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));
}

bool Terrain::isWater(float x, float z) const { return !mapped() && groundHeight(x, z) < 0.05f; }

float Terrain::forestDensity(float x, float z) const {
  if (mapped()) return 0.0f;
  float n = fbm(x / 2200.0f + 40.0f, z / 2200.0f - 13.0f, 4, seed_ + 71);
  return smoothstep(0.05f, 0.35f, n);
}

}  // namespace world
