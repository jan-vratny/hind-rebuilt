#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "world/terrain.hpp"

// War zones of the original game (AFGHAN.WZ, KAZAK.WZ, KOREA.WZ, SARATOV.WZ):
//  GRIDPOST.BIN  257 x 257 posts of 8 bytes, x (east) the outer index, y (north) the inner one,
//                4096 ft apart: u16 height in feet (river beds and valley floors are 0), u8
//                shading, two u8 colours of the zone palette (3DPAL.RGB, with the shading baked
//                in), u8 flags, two u8 surface types (bit 7: flattened, such as rivers).
//  FXGRDPST.BIN  the same posts with 18 bytes: height, shading, normals of the cell's triangles.
//  AIRFIELD.BIN  u32 count, then 32-byte records: name, ILS frequency, u32 x and y.
//  GNDOBJS.BIN   the buildings, bridges and scrub standing about the zone: 64 x 64 cells of
//                16384 ft (x the outer index), a u32 offset of each cell's list (0: none); a list
//                is a u16 count and 12-byte records of u16 object (3DOBJS.BIN index - 1), x and
//                y in ft within the cell, heading (65536 a turn), two u16 (0, 6).
// World coordinates of the missions are in 1/256 ft, x east and y north.
namespace gamedata {

constexpr float kFeet = 0.3048f;
constexpr float kZoneUnit = kFeet / 256.0f;  // m per unit of the zone and mission coordinates

struct ZoneAirfield {
  std::string name;
  glm::vec2 pos{0.0f};  // m, x east, y north
};

struct ZoneObject {
  int object = 0;       // 3DOBJS.BIN index
  glm::vec2 pos{0.0f};  // m, x east, y north
  float heading = 0.0f; // rad, clockwise from north
};

struct Zone {
  std::string name;
  int size = 0;          // posts per side
  float spacing = 0.0f;  // m between posts
  std::vector<float> height;       // m, [x * size + y]
  std::vector<glm::vec3> colour;   // [x * size + y], 0..1, with the baked-in shading toned down
  std::vector<ZoneAirfield> airfields;
  std::vector<ZoneObject> objects;

  const ZoneAirfield* airfield(const std::string& name) const;
  // The landscape for world::Terrain with `centre` (m, east and north) at the world origin and
  // `base` metres added to every height.
  world::HeightMap heightMap(glm::vec2 centre, float base) const;
};

class OriginalData;

// The runways of the airfield at `centre` (m, east and north), as the markings standing about it
// lay them out (the original draws no runway of its own): lines of centre-line stripes, the
// numbers at the ends, the touchdown marks. World x, z with `centre` at the world origin.
std::vector<world::Runway> zoneRunways(const Zone& zone, const OriginalData& original, glm::vec2 centre);

// The zone's airfields where its runway markings stand (Kazakhstan and Saratov carry a copy of
// Korea's AIRFIELD.BIN, not their own): one at the middle of each cluster of markings, the
// biggest first, named "AIRFIELD 1", "AIRFIELD 2"...
std::vector<ZoneAirfield> airfieldsFromMarkings(const Zone& zone, const OriginalData& original);

// `zoneDir` is a directory such as "AFGHAN.WZ" in the game directory.
bool loadZone(const std::string& gameDir, const std::string& zoneDir, Zone& out, std::string& error);

}  // namespace gamedata
