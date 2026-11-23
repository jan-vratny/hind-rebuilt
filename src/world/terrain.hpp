#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// Procedural world. Coordinates: x = east, y = up, z = south (metres).
// The ground the helicopter touches is exactly the rendered LOD0 triangle surface.
namespace world {

constexpr float kCell = 50.0f;  // LOD0 grid spacing

uint32_t hash2(int x, int z, uint32_t seed);
inline float hashUnit(uint32_t h) { return (h & 0xFFFFFF) / float(0x1000000); }

// A runway of a war zone's airfield: its ends (world x, z) and width, m.
struct Runway {
  glm::vec2 a{0.0f}, b{0.0f};
  float width = 50.0f;
};
// A paved area of a war zone's airfield (an apron, a taxiway), as the original's map has it
// (FEATURES.BIN): its outline (world x, z) and colour.
struct Pad {
  std::vector<glm::vec2> outline;
  glm::vec3 colour{0.47f};
};

struct Airfield {
  glm::vec2 center{0.0f, 0.0f};
  float elevation = 0.0f;
  float flatRadius = 900.0f;
  float blendRadius = 1800.0f;
  // Runway runs north-south through the centre, apron east of it.
  float runwayLength = 1800.0f, runwayWidth = 45.0f;
  glm::vec2 apronCenter{220.0f, 0.0f};
  glm::vec2 apronHalf{110.0f, 160.0f};
  // The war zone's own runways, as the original has them (its buildings stand about them as
  // scenery); none: the airfield above, with its apron and buildings.
  std::vector<Runway> runways;
  // Its aprons and taxiways (those on the levelled ground about the runways).
  std::vector<Pad> pads;
};

// A landscape given as heights on a square grid of posts, such as a war zone of the original
// game. Between the posts the ground is interpolated, with a little procedural detail.
struct HeightMap {
  int size = 0;                   // posts per side
  float spacing = 0.0f;           // m between posts
  glm::vec2 origin{0.0f};         // world x, z of post (0, 0); i runs east (+x), j south (+z)
  std::vector<float> height;      // m, [j * size + i]
  std::vector<glm::vec3> colour;  // ground colour at the posts, 0..1 (may be empty)
};

class Terrain {
public:
  explicit Terrain(uint32_t seed = 1996);
  // A mapped landscape; the airfield is at the world origin, or the ground under `runways` is
  // levelled for them. No sea, no forests.
  explicit Terrain(HeightMap map, std::vector<Runway> runways = {}, std::vector<Pad> pads = {}, uint32_t seed = 1996);

  float rawHeight(float x, float z) const;         // smooth field, before grid quantisation
  float vertexHeight(int ix, int iz) const;        // height at grid vertex (ix*kCell, iz*kCell)
  float groundHeight(float x, float z) const;      // on the LOD0 triangle surface
  glm::vec3 groundNormal(float x, float z) const;  // normal of that triangle
  bool isWater(float x, float z) const;
  // 0..1 forest density used by the scatter generator
  float forestDensity(float x, float z) const;

  const Airfield& airfield() const { return airfield_; }
  bool mapped() const { return map_.size > 1; }
  // Ground colour from the map (mapped landscapes only).
  glm::vec3 mapColour(float x, float z) const;
  uint32_t seed() const { return seed_; }

private:
  float naturalHeight(float x, float z) const;
  float mapHeight(float x, float z) const;
  uint32_t seed_;
  Airfield airfield_;
  HeightMap map_;
};

}  // namespace world
