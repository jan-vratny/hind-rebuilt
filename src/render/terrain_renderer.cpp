#include "render/terrain_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "render/models.hpp"

namespace render {

namespace {

using world::hash2;
using world::hashUnit;
using world::kCell;

int lodForDistance(float d) {
  if (d < 2400.0f) return 0;
  if (d < 4800.0f) return 1;
  if (d < 9600.0f) return 2;
  return 3;
}

struct Frustum {
  std::array<glm::vec4, 6> planes;
  explicit Frustum(const glm::mat4& m) {
    glm::vec4 r0{m[0][0], m[1][0], m[2][0], m[3][0]};
    glm::vec4 r1{m[0][1], m[1][1], m[2][1], m[3][1]};
    glm::vec4 r2{m[0][2], m[1][2], m[2][2], m[3][2]};
    glm::vec4 r3{m[0][3], m[1][3], m[2][3], m[3][3]};
    planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
  }
  bool visible(glm::vec3 lo, glm::vec3 hi) const {
    for (const glm::vec4& p : planes) {
      glm::vec3 v{p.x > 0 ? hi.x : lo.x, p.y > 0 ? hi.y : lo.y, p.z > 0 ? hi.z : lo.z};
      if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0.0f) return false;
    }
    return true;
  }
};

const glm::vec3 kFieldColors[] = {
    {0.33f, 0.46f, 0.20f}, {0.40f, 0.51f, 0.22f}, {0.47f, 0.52f, 0.26f}, {0.36f, 0.43f, 0.19f},
    {0.56f, 0.54f, 0.30f}, {0.46f, 0.38f, 0.25f}, {0.30f, 0.40f, 0.18f}, {0.52f, 0.50f, 0.28f},
};

glm::vec3 mix3(glm::vec3 a, glm::vec3 b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }

}  // namespace

void TerrainRenderer::init() { buildAirfield(); }

glm::vec3 TerrainRenderer::groundColor(glm::vec3 c, glm::vec3 n, int ix, int iz) const {
  float jitter = 0.94f + 0.12f * hashUnit(hash2(ix, iz, 991));
  if (terrain_.mapped()) return terrain_.mapColour(c.x, c.z) * jitter;
  if (c.y < 0.05f) return {0.17f, 0.31f, 0.46f};
  const auto& af = terrain_.airfield();
  float slope = 1.0f - n.y;
  glm::vec3 col;
  float dAir = glm::length(glm::vec2(c.x, c.z) - af.center);
  if (dAir < af.flatRadius) {
    col = {0.40f, 0.50f, 0.25f};
  } else {
    // Patchwork of fields, each covering several cells.
    uint32_t field = hash2(int(std::floor(c.x / 230.0f)), int(std::floor(c.z / 170.0f)), terrain_.seed() + 5);
    col = kFieldColors[field % 8];
    float forest = terrain_.forestDensity(c.x, c.z);
    if (forest > 0.3f) col = mix3(col, {0.17f, 0.29f, 0.13f}, (forest - 0.3f) * 3.0f);
    col = mix3(col, {0.46f, 0.42f, 0.29f}, (c.y - 380.0f) / 250.0f);   // upland scrub
    col = mix3(col, {0.50f, 0.48f, 0.45f}, (c.y - 850.0f) / 250.0f);   // bare rock
    col = mix3(col, {0.47f, 0.40f, 0.31f}, (slope - 0.18f) * 6.0f);   // steep faces
    if (c.y > 1250.0f && slope < 0.35f) col = mix3(col, {0.92f, 0.93f, 0.95f}, (c.y - 1250.0f) / 120.0f);
    if (c.y < 6.0f) col = mix3({0.70f, 0.65f, 0.48f}, col, c.y / 6.0f);  // beaches
  }
  return col * jitter;
}

void TerrainRenderer::buildChunk(int cx, int cz, int lod, Chunk& out) const {
  MeshBuilder mb;
  const int step = 1 << lod;
  const int n = kChunkCells / step;
  const int ix0 = cx * kChunkCells, iz0 = cz * kChunkCells;
  // Heights on this LOD's grid, including one ring for skirts.
  std::vector<glm::vec3> grid((n + 1) * (n + 1));
  float minY = 1e9f, maxY = -1e9f;
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) {
      int ix = ix0 + i * step, iz = iz0 + j * step;
      float h = terrain_.vertexHeight(ix, iz);
      grid[j * (n + 1) + i] = {ix * kCell, h, iz * kCell};
      minY = std::min(minY, h);
      maxY = std::max(maxY, h);
    }
  auto g = [&](int i, int j) { return grid[j * (n + 1) + i]; };
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      glm::vec3 p00 = g(i, j), p10 = g(i + 1, j), p01 = g(i, j + 1), p11 = g(i + 1, j + 1);
      int ix = ix0 + i * step, iz = iz0 + j * step;
      glm::vec3 nA = glm::normalize(glm::cross(p11 - p00, p10 - p00));
      glm::vec3 nB = glm::normalize(glm::cross(p01 - p00, p11 - p00));
      const bool sea = !terrain_.mapped();
      bool waterA = sea && p00.y < 0.05f && p10.y < 0.05f && p11.y < 0.05f;
      bool waterB = sea && p00.y < 0.05f && p11.y < 0.05f && p01.y < 0.05f;
      glm::vec3 cA = (p00 + p10 + p11) / 3.0f, cB = (p00 + p11 + p01) / 3.0f;
      if (!waterA && cA.y < 0.05f) cA.y = 0.06f;
      if (!waterB && cB.y < 0.05f) cB.y = 0.06f;
      mb.tri(p00, p11, p10, groundColor(cA, nA, ix * 2, iz));
      mb.tri(p00, p01, p11, groundColor(cB, nB, ix * 2 + 1, iz));
    }
  // Skirts hide cracks between neighbouring LODs.
  float depth = 15.0f * step + 10.0f;
  auto skirt = [&](glm::vec3 a, glm::vec3 b) {
    glm::vec3 col = groundColor((a + b) * 0.5f, {0, 1, 0}, int(a.x), int(a.z)) * 0.8f;
    mb.quad(a, b, b - glm::vec3(0, depth, 0), a - glm::vec3(0, depth, 0), col);
  };
  for (int i = 0; i < n; ++i) {
    skirt(g(i, 0), g(i + 1, 0));
    skirt(g(i, n), g(i + 1, n));
    skirt(g(0, i), g(0, i + 1));
    skirt(g(n, i), g(n, i + 1));
  }
  if (lod <= 1) scatter(cx, cz, mb);
  for (const Vertex& v : mb.verts) maxY = std::max(maxY, v.pos.y);
  out.mesh.upload(mb.verts);
  out.minY = minY - depth;
  out.maxY = maxY;
}

void TerrainRenderer::scatter(int cx, int cz, MeshBuilder& mb) const {
  const auto& af = terrain_.airfield();
  const uint32_t seed = terrain_.seed();
  const int ix0 = cx * kChunkCells, iz0 = cz * kChunkCells;
  auto nearAirfield = [&](float x, float z) {
    return glm::length(glm::vec2(x, z) - af.center) < af.flatRadius + 150.0f;
  };

  // Forest trees.
  for (int j = 0; j < kChunkCells; ++j)
    for (int i = 0; i < kChunkCells; ++i) {
      float x0 = (ix0 + i) * kCell, z0 = (iz0 + j) * kCell;
      float forest = terrain_.forestDensity(x0 + 25.0f, z0 + 25.0f);
      if (forest <= 0.05f) continue;
      uint32_t h = hash2(ix0 + i, iz0 + j, seed + 301);
      int count = int(forest * 3.0f + hashUnit(h));
      for (int t = 0; t < count; ++t) {
        uint32_t ht = hash2(int(h), t, seed + 302);
        float x = x0 + 4.0f + 42.0f * hashUnit(ht);
        float z = z0 + 4.0f + 42.0f * hashUnit(ht >> 7 | ht << 25);
        if (nearAirfield(x, z)) continue;
        float y = terrain_.groundHeight(x, z);
        if (y < 3.0f || y > 900.0f || terrain_.groundNormal(x, z).y < 0.82f) continue;
        float s = 0.8f + 0.5f * hashUnit(ht >> 3);
        glm::vec3 col = glm::vec3(0.13f, 0.27f, 0.12f) * (0.8f + 0.4f * hashUnit(ht >> 11));
        mb.pyramid({x, y - 0.5f, z}, 3.2f * s, 13.0f * s, hashUnit(ht >> 5) * 1.6f, col);
      }
    }

  // Occasional village in flat lowland (the original's zones will bring their own).
  uint32_t vh = hash2(cx, cz, seed + 401);
  if (!terrain_.mapped() && hashUnit(vh) < 0.16f) {
    float vx = (cx + 0.5f) * kChunkSize, vz = (cz + 0.5f) * kChunkSize;
    float vy = terrain_.groundHeight(vx, vz);
    if (vy > 5.0f && vy < 500.0f && terrain_.forestDensity(vx, vz) < 0.3f && !nearAirfield(vx, vz) &&
        terrain_.groundNormal(vx, vz).y > 0.95f) {
      int houses = 8 + int(hashUnit(vh >> 4) * 10.0f);
      float streetRot = hashUnit(vh >> 9) * 3.14159f;
      const glm::vec3 walls[] = {{0.86f, 0.84f, 0.76f}, {0.78f, 0.72f, 0.60f}, {0.70f, 0.70f, 0.68f}};
      const glm::vec3 roofs[] = {{0.55f, 0.22f, 0.16f}, {0.40f, 0.30f, 0.24f}, {0.45f, 0.46f, 0.48f}};
      for (int k = 0; k < houses; ++k) {
        uint32_t hk = hash2(int(vh), k, seed + 402);
        // Two rows along a street.
        float along = (k / 2 - houses / 4) * 24.0f + hashUnit(hk) * 6.0f;
        float across = (k % 2 ? 1.0f : -1.0f) * (16.0f + hashUnit(hk >> 5) * 6.0f);
        float cs = std::cos(streetRot), sn = std::sin(streetRot);
        float x = vx + along * cs - across * sn, z = vz + along * sn + across * cs;
        float y = terrain_.groundHeight(x, z);
        if (y < 2.0f) continue;
        float w = 8.0f + hashUnit(hk >> 8) * 5.0f, d = 6.0f + hashUnit(hk >> 12) * 3.0f;
        mb.house({x, y, z}, w, d, 3.5f + hashUnit(hk >> 16) * 1.5f, 2.8f, streetRot, walls[hk % 3],
                 roofs[(hk >> 2) % 3]);
      }
    }
  }
}

void TerrainRenderer::buildAirfield() {
  const auto& af = terrain_.airfield();
  MeshBuilder mb;
  const float y = af.elevation + 0.12f;
  auto flat = [&](float x0, float z0, float x1, float z1, glm::vec3 col, float dy = 0.0f) {
    mb.quad({x0, y + dy, z0}, {x0, y + dy, z1}, {x1, y + dy, z1}, {x1, y + dy, z0}, col);
  };
  if (!af.runways.empty()) {
    // The war zone's aprons and taxiways under its runways, as the original's map has them.
    for (const world::Pad& p : af.pads)
      for (size_t i = 1; i + 1 < p.outline.size(); ++i) {
        auto at = [&](glm::vec2 q) { return glm::vec3(q.x, y - 0.09f, q.y); };
        mb.tri(at(p.outline[0]), at(p.outline[i]), at(p.outline[i + 1]), p.colour);
      }
    // The war zone's runways; their markings and buildings stand about them as scenery.
    for (const world::Runway& r : af.runways) {
      const glm::vec2 d = glm::normalize(r.b - r.a), s = glm::vec2(-d.y, d.x) * (r.width * 0.5f);
      auto at = [&](glm::vec2 p) { return glm::vec3(p.x, y - 0.06f, p.y); };
      // In the paved areas' grey, as the original paints them all (its map's colour 11).
      mb.quad(at(r.a - s), at(r.a + s), at(r.b + s), at(r.b - s), af.pads.empty() ? glm::vec3(0.30f, 0.30f, 0.31f) : af.pads[0].colour);
    }
    airfield_.upload(mb.verts);
    return;
  }
  float hl = af.runwayLength * 0.5f, hw = af.runwayWidth * 0.5f;
  flat(-hw, -hl, hw, hl, {0.30f, 0.30f, 0.31f});
  for (float z = -hl + 40.0f; z < hl - 60.0f; z += 60.0f) flat(-0.6f, z, 0.6f, z + 30.0f, {0.85f, 0.85f, 0.82f}, 0.03f);
  for (int i = -4; i <= 4; ++i) {  // threshold bars
    if (i == 0) continue;
    float x = i * 4.5f;
    flat(x - 1.2f, -hl + 6.0f, x + 1.2f, -hl + 26.0f, {0.85f, 0.85f, 0.82f}, 0.03f);
    flat(x - 1.2f, hl - 26.0f, x + 1.2f, hl - 6.0f, {0.85f, 0.85f, 0.82f}, 0.03f);
  }
  // Taxiway to the apron and the apron itself.
  flat(hw, -12.0f, af.apronCenter.x - af.apronHalf.x, 12.0f, {0.36f, 0.36f, 0.36f});
  glm::vec2 a0 = af.apronCenter - af.apronHalf, a1 = af.apronCenter + af.apronHalf;
  flat(a0.x, a0.y, a1.x, a1.y, {0.45f, 0.45f, 0.44f});
  // Helipad circles (as squares with a cross) on the apron rows.
  for (int k = 0; k < 4; ++k) {
    float pz = -120.0f + k * 60.0f, px = af.apronCenter.x - 40.0f;
    flat(px - 9.0f, pz - 9.0f, px + 9.0f, pz + 9.0f, {0.38f, 0.38f, 0.37f}, 0.02f);
    flat(px - 0.5f, pz - 6.0f, px + 0.5f, pz + 6.0f, {0.85f, 0.80f, 0.30f}, 0.04f);
    flat(px - 6.0f, pz - 0.5f, px + 6.0f, pz + 0.5f, {0.85f, 0.80f, 0.30f}, 0.04f);
  }
  // Buildings east of the apron.
  float bx = a1.x + 40.0f;
  for (int k = 0; k < 3; ++k) {
    float z = -110.0f + k * 70.0f;
    mb.house({bx + 20.0f, y, z}, 50.0f, 40.0f, 11.0f, 6.0f, 1.5708f, {0.55f, 0.58f, 0.55f}, {0.42f, 0.46f, 0.44f});
  }
  // Control tower.
  glm::mat4 id(1.0f);
  mb.box(id, {bx - 5.0f, y, 130.0f}, {bx + 5.0f, y + 14.0f, 140.0f}, {0.80f, 0.78f, 0.72f});
  mb.box(id, {bx - 6.5f, y + 14.0f, 128.5f}, {bx + 6.5f, y + 18.0f, 141.5f}, {0.25f, 0.33f, 0.40f});
  mb.box(id, {bx - 7.0f, y + 18.0f, 128.0f}, {bx + 7.0f, y + 18.6f, 142.0f}, {0.45f, 0.45f, 0.45f});
  // Fuel tanks and bowsers.
  for (int k = 0; k < 3; ++k) {
    float z = 175.0f + k * 14.0f;
    mb.box(id, {bx - 5.0f, y, z - 5.0f}, {bx + 5.0f, y + 7.0f, z + 5.0f}, {0.62f, 0.64f, 0.58f});
  }
  for (int k = 0; k < 2; ++k) {
    float x = a0.x + 20.0f + k * 12.0f;
    mb.box(id, {x - 1.3f, y, 140.0f}, {x + 1.3f, y + 2.8f, 148.0f}, {0.33f, 0.38f, 0.26f});
  }
  // Parked Hinds on the pads (the player starts on the last one).
  for (int k = 0; k < 3; ++k) {
    float pz = -120.0f + k * 60.0f, px = af.apronCenter.x - 40.0f;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(px, y + 1.66f, pz));
    buildMi24(mb, m, Mi24Parts::Parked);
  }
  airfield_.upload(mb.verts);
}

void TerrainRenderer::draw(const Program& prog, const glm::vec3& cam, const glm::mat4& viewProj, bool unlimited) {
  ++frame_;
  Frustum fr(viewProj);
  const int range = int(std::ceil(viewDistance_ / kChunkSize)) + 1;
  const int ccx = int(std::floor(cam.x / kChunkSize)), ccz = int(std::floor(cam.z / kChunkSize));

  struct Want {
    int cx, cz, lod;
    float dist;
  };
  std::vector<Want> wanted;
  wanted.reserve(size_t(4 * range * range));
  for (int dz = -range; dz <= range; ++dz)
    for (int dx = -range; dx <= range; ++dx) {
      int cx = ccx + dx, cz = ccz + dz;
      glm::vec2 c{(cx + 0.5f) * kChunkSize, (cz + 0.5f) * kChunkSize};
      glm::vec2 d = glm::max(glm::abs(c - glm::vec2(cam.x, cam.z)) - glm::vec2(kChunkSize * 0.5f), glm::vec2(0.0f));
      float dist = glm::length(d);
      if (dist > viewDistance_) continue;
      wanted.push_back({cx, cz, lodForDistance(dist), dist});
    }
  std::sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.dist < b.dist; });

  auto t0 = std::chrono::steady_clock::now();
  glm::mat4 id(1.0f);
  prog.set("uModel", id);
  // Push the terrain back slightly so the airfield markings never z-fight with it.
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.0f, 2.0f);
  for (const Want& w : wanted) {
    glm::vec3 lo{w.cx * kChunkSize, -100.0f, w.cz * kChunkSize};
    glm::vec3 hi{lo.x + kChunkSize, 2500.0f, lo.z + kChunkSize};
    auto it = chunks_.find(key(w.cx, w.cz, w.lod));
    if (it == chunks_.end()) {
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      if (unlimited || ms < 5.0) {
        Chunk& c = chunks_[key(w.cx, w.cz, w.lod)];
        buildChunk(w.cx, w.cz, w.lod, c);
        it = chunks_.find(key(w.cx, w.cz, w.lod));
      } else {
        // Not built yet: draw any other LOD we have.
        for (int l = 0; l < 4 && it == chunks_.end(); ++l) {
          auto alt = chunks_.find(key(w.cx, w.cz, l));
          if (alt != chunks_.end()) it = alt;
        }
        if (it == chunks_.end()) continue;
      }
    }
    Chunk& c = it->second;
    c.lastUsed = frame_;
    lo.y = c.minY;
    hi.y = c.maxY;
    if (fr.visible(lo, hi)) c.mesh.draw();
  }
  glDisable(GL_POLYGON_OFFSET_FILL);
  airfield_.draw();

  // Drop chunks unused for a while.
  if (frame_ % 60 == 0) {
    for (auto it = chunks_.begin(); it != chunks_.end();) {
      if (frame_ - it->second.lastUsed > 180) it = chunks_.erase(it);
      else ++it;
    }
  }
}

}  // namespace render
