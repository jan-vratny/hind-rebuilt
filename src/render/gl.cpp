#include "render/gl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

namespace render {

namespace {

GLuint compile(const char* name, GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader %s (%s): %s\n", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

}  // namespace

Program::~Program() {
  if (id_) glDeleteProgram(id_);
}

bool Program::build(const char* name, const char* vs, const char* fs) {
  GLuint v = compile(name, GL_VERTEX_SHADER, vs);
  GLuint f = compile(name, GL_FRAGMENT_SHADER, fs);
  if (!v || !f) return false;
  id_ = glCreateProgram();
  glAttachShader(id_, v);
  glAttachShader(id_, f);
  glLinkProgram(id_);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(id_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(id_, sizeof log, nullptr, log);
    std::fprintf(stderr, "program %s: %s\n", name, log);
    return false;
  }
  return true;
}

void Program::set(const char* u, const glm::mat4& m) const { glUniformMatrix4fv(loc(u), 1, GL_FALSE, glm::value_ptr(m)); }
void Program::set(const char* u, const glm::vec3& v) const { glUniform3fv(loc(u), 1, glm::value_ptr(v)); }
void Program::set(const char* u, const glm::vec4& v) const { glUniform4fv(loc(u), 1, glm::value_ptr(v)); }
void Program::set(const char* u, float f) const { glUniform1f(loc(u), f); }

GpuMesh::GpuMesh(GpuMesh&& o) noexcept { *this = std::move(o); }

GpuMesh& GpuMesh::operator=(GpuMesh&& o) noexcept {
  if (this != &o) {
    destroy();
    std::swap(vao_, o.vao_);
    std::swap(vbo_, o.vbo_);
    std::swap(count_, o.count_);
  }
  return *this;
}

GpuMesh::~GpuMesh() { destroy(); }

void GpuMesh::destroy() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  vao_ = vbo_ = 0;
  count_ = 0;
}

void uploadAtlas(const std::vector<uint8_t>& rgba, int count) {
  static GLuint tex = 0;
  if (!tex) glGenTextures(1, &tex);
  std::vector<uint8_t> atlas(size_t(kAtlasSize) * kAtlasSize * 4, 255);
  const size_t tile = size_t(kAtlasTile) * kAtlasTile * 4;
  for (int t = 0; t < std::min(count, kAtlasTiles * kAtlasTiles) && size_t(t + 1) * tile <= rgba.size(); ++t)
    for (int y = 0; y < kAtlasTile; ++y)
      std::memcpy(&atlas[((size_t(t / kAtlasTiles) * kAtlasTile + y) * kAtlasSize + size_t(t % kAtlasTiles) * kAtlasTile) * 4],
                  &rgba[size_t(t) * tile + size_t(y) * kAtlasTile * 4], size_t(kAtlasTile) * 4);
  glActiveTexture(GL_TEXTURE0 + kAtlasUnit);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasSize, kAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
  // Texels as the original drew them: hard edged.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
}

void GpuMesh::upload(const std::vector<Vertex>& v) {
  if (!vao_) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tex));
  } else {
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  }
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(Vertex)), v.data(), GL_STATIC_DRAW);
  count_ = GLsizei(v.size());
}

void GpuMesh::draw() const {
  if (!count_) return;
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLES, 0, count_);
}

}  // namespace render
