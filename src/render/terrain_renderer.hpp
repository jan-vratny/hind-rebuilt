#pragma once
#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include "render/gl.hpp"
#include "render/mesh.hpp"
#include "world/terrain.hpp"

namespace render {

// Streams terrain chunks around the camera with distance-based LOD. Trees and villages are baked
// into the near chunks; the airfield is a separate static mesh.
class TerrainRenderer {
public:
  static constexpr int kChunkCells = 16;
  static constexpr float kChunkSize = kChunkCells * world::kCell;

  explicit TerrainRenderer(const world::Terrain& terrain) : terrain_(terrain) {}
  void init();
  void setViewDistance(float d) { viewDistance_ = d; }
  // Builds missing chunks (bounded work unless `unlimited`) and draws the visible ones.
  // The scene program must be bound; uModel is set per chunk.
  void draw(const Program& prog, const glm::vec3& camPos, const glm::mat4& viewProj, bool unlimited = false);

private:
  struct Chunk {
    GpuMesh mesh;
    float minY = 0, maxY = 0;
    int lastUsed = 0;
  };
  static uint64_t key(int cx, int cz, int lod) {
    return (uint64_t(uint32_t(cx)) << 34) ^ (uint64_t(uint32_t(cz)) << 4) ^ uint64_t(lod);
  }
  void buildChunk(int cx, int cz, int lod, Chunk& out) const;
  void scatter(int cx, int cz, MeshBuilder& mb) const;
  void buildAirfield();
  glm::vec3 groundColor(glm::vec3 center, glm::vec3 n, int ix, int iz) const;

  const world::Terrain& terrain_;
  std::unordered_map<uint64_t, Chunk> chunks_;
  GpuMesh airfield_;
  float viewDistance_ = 16000.0f;
  int frame_ = 0;
};

}  // namespace render
