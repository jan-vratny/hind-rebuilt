#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render/glcore.hpp"

namespace render {

class Program {
public:
  Program() = default;
  Program(const Program&) = delete;
  Program& operator=(const Program&) = delete;
  ~Program();
  bool build(const char* name, const char* vertexSrc, const char* fragmentSrc);
  void use() const { glUseProgram(id_); }
  GLint loc(const char* uniform) const { return glGetUniformLocation(id_, uniform); }
  void set(const char* u, const glm::mat4& m) const;
  void set(const char* u, const glm::vec3& v) const;
  void set(const char* u, const glm::vec4& v) const;
  void set(const char* u, float f) const;

private:
  GLuint id_ = 0;
};

// Flat-shaded vertex: every triangle has its own vertices, so normal and colour are per face.
struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec4 color;
  // Where in the texture atlas (kAtlasSize) a textured face takes its colour, multiplied by
  // `color`; z < 0: a plain face.
  glm::vec3 tex{0.0f, 0.0f, -1.0f};
};

// The atlas of the original's object textures: 8 x 8 tiles of 64 x 64 texels, bound to texture
// unit 1 for the scene shader.
constexpr int kAtlasTile = 64, kAtlasTiles = 8, kAtlasSize = kAtlasTile * kAtlasTiles;
constexpr int kAtlasUnit = 1;
// Uploads the textures (RGBA, kAtlasTile square each, at most kAtlasTiles^2) and binds the atlas.
void uploadAtlas(const std::vector<uint8_t>& rgba, int count);

class GpuMesh {
public:
  GpuMesh() = default;
  GpuMesh(const GpuMesh&) = delete;
  GpuMesh& operator=(const GpuMesh&) = delete;
  GpuMesh(GpuMesh&& o) noexcept;
  GpuMesh& operator=(GpuMesh&& o) noexcept;
  ~GpuMesh();
  void upload(const std::vector<Vertex>& v);
  void draw() const;
  bool empty() const { return count_ == 0; }

private:
  void destroy();
  GLuint vao_ = 0, vbo_ = 0;
  GLsizei count_ = 0;
};

}  // namespace render
