#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// What the original's planning map draws of a war zone:
//  FEATURES.BIN  u32 count, u32 offsets of the features, each the drawing of one 16384 ft cell:
//                three u32 offsets (its vertices, areas, lines); the vertices a u16 count and
//                records of a type byte (high nibble: inside the cell, on its edges; bit 2: a
//                fine position follows, bit 3: a second one), u8 x and y | 0x80 in 1024 ft, the
//                fine x and y in 16 ft (32 = none), the second pair; areas and lines a u16 count
//                and records of u8 (2 area, 1 line), u8 kind (0 ground, 1 water, 4 road,
//                runway and town, 5 track, 6 other), u8 n, u8 colour of 3DPAL, n vertex indices.
//                An area's edge given again as a line (its first byte 2) is not drawn.
//  FEATURE1.LYR  64 x 64 u32 (x the outer index): the feature of each cell + 1, 0 for none.
//  3DPAL.RGB     its entries 192 .. 207 are the map's colours: the ground from dark to light
//                (192 .. 198), contours, water, tracks, roads, the grid, red, blue, orange, white.
//  GRIDPOST.BIN  the heights for the contours (every 250 ft) and the ground's facets; the
//                cells are split into two triangles, the diagonal from the south-west corner
//                where x + y is even, from the north-west one where it is odd.
namespace gamedata {

struct ZoneMap {
  enum Kind : uint8_t { Ground = 0, Water = 1, Road = 4, Track = 5, Other = 6, Contour = 100 };
  struct Shape {
    Kind kind = Ground;
    bool area = false;
    std::vector<glm::vec2> pts;  // m, x east, y north
    float level = 0.0f;          // a contour's height, ft
    uint8_t colour = 0;          // of 3DPAL.RGB (the zone's palette)
  };
  static constexpr int kCells = 64;
  static constexpr float kContourFt = 250.0f;

  int size = 0;          // posts per side
  float spacing = 0.0f;  // m between posts
  std::vector<float> heightFt;      // [x * size + y]
  std::vector<uint8_t> colour[2];   // the two triangles' colours of each post's cell
  std::array<glm::u8vec3, 16> palette{};
  std::vector<Shape> shapes;
  std::vector<std::vector<uint32_t>> cells;  // [cx * kCells + cy]: shapes in the cell
  float cellSize() const { return spacing * float(size - 1) / float(kCells); }

  bool loaded() const { return size > 1; }
  // The colour of a map palette entry 192 + i.
  glm::u8vec3 rgb(int i) const { return palette[size_t(i)]; }
  float height(int x, int y) const { return heightFt[size_t(x) * size_t(size) + size_t(y)]; }
  // Whether the triangle of cell (x, y) at (fx, fy) in the cell is the first of the two.
  static bool firstTriangle(int x, int y, float fx, float fy) { return (x + y) % 2 == 0 ? fx > fy : fx + fy < 1.0f; }
};

bool loadZoneMap(const std::string& gameDir, const std::string& zoneDir, ZoneMap& out, std::string& error);

}  // namespace gamedata
