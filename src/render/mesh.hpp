#pragma once
#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "render/gl.hpp"

namespace render {

// CPU-side builder of flat-shaded triangle soups.
class MeshBuilder {
public:
  std::vector<Vertex> verts;

  void tri(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color);
  void tri(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 color) { tri(a, b, c, glm::vec4(color, 1.0f)); }
  void quad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 color);
  // A convex polygon of the original game's models (transformed by m): one-sided ones go round
  // clockwise seen from the front and come out counter-clockwise (OpenGL's front faces, for
  // drawing with back faces culled); the others twice, once each way.
  void face(const std::vector<glm::vec3>& pts, glm::vec3 color, bool oneSided, const glm::mat4& m = glm::mat4(1.0f));
  // The same with a texture of the atlas (gl.hpp): `uv` the texel of each corner in its tile
  // `texture`; no texture (< 0 or no uv): the plain colour.
  void face(const std::vector<glm::vec3>& pts, glm::vec3 color, bool oneSided, const glm::mat4& m,
            const std::vector<glm::vec2>& uv, int texture);
  // Box between two corners, transformed by m.
  void box(const glm::mat4& m, glm::vec3 lo, glm::vec3 hi, glm::vec3 color);
  // Hexahedron given its 8 corners: 0-3 at one end (CCW), 4-7 at the other end.
  void hexa(const glm::vec3 (&c)[8], glm::vec3 color);
  // Square pyramid standing on `base`.
  void pyramid(glm::vec3 base, float halfWidth, float height, float rotation, glm::vec3 color);
  // House: walls plus a gable roof, footprint w x d centred on base, rotated about y.
  void house(glm::vec3 base, float w, float d, float wallH, float roofH, float rotation, glm::vec3 wall,
             glm::vec3 roof);

  // Colour of one loft facet: ring index (station), segment index around the ring, facet centre
  // in local coordinates. A negative red component leaves the facet out.
  using FacetColor = std::function<glm::vec3(int ring, int seg, glm::vec3 centre)>;
  // Hull lofted through rings of points (all rings have the same count). The ends are capped.
  void loft(const glm::mat4& m, const std::vector<std::vector<glm::vec3>>& rings, const FacetColor& color,
            bool capFront = true, bool capBack = true);
  // Cylinder or cone from a to b with `sides` facets; caps get capColor.
  void cylinder(const glm::mat4& m, glm::vec3 a, glm::vec3 b, float ra, float rb, int sides, glm::vec3 color,
                glm::vec3 capColor);
  void cylinder(const glm::mat4& m, glm::vec3 a, glm::vec3 b, float ra, float rb, int sides, glm::vec3 color) {
    cylinder(m, a, b, ra, rb, sides, color, color);
  }
  // Flat plate: convex outline given in order, thickened by +-thick/2.
  void slab(const glm::mat4& m, const std::vector<glm::vec3>& outline, glm::vec3 thick, glm::vec3 color);
};

}  // namespace render
