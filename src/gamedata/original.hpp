#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Reader of the 3D objects of the original game (DI Hind, 1996): 3DDATA/3DOBJS.BIN with the
// palette 3DDATA/3DPAL.RGB, read straight from the player's own installation. Nothing of the
// original is stored with the remake.
//
// 3DOBJS.BIN: u32 object count N, then u32 offsets of objects 1..N. An object starts with a
// header (bounding box at 0x0c in 1/16 vertex units, five u32 section offsets at 0x30, short
// name at 0x48, long name at 0x58, sub-object placements from 0xbc), then the sections: vertices
// (u16 count, int16 x,y,z: x right, y forward, z up; packed when the header's first u32 is 1, as
// the buildings, bridges and copses are: bytes x, y, z in steps of 256 units, then bytes x, y, z
// in steps of 4, each + 32), normals, and a display list of BSP nodes
// and polygons whose colour is an index into the palette. A polygon's type says what follows its
// vertex count and colour (a face normal, vertex normals, texture coordinates after each vertex
// index on the aircraft's textured faces); type 0x06 are the sectors of a turning rotor's disc.
// The textured faces (0x0f: index, u, v; 0x13: index, vertex normal, u, v) name their texture
// in their sixth byte as 2 + 6 x its number in OBJTEXT.BIN: a table of 24-byte entries from
// byte 4 (the texture's offset at +8 of its entry), each texture 12 bytes of header (2, 64, 0)
// and 64 x 64 bytes of the palette.
// Command 5 holds variants of a part (swing wings), only the first is used. Each object comes in
// three levels of detail stored one after another, the most detailed first.
namespace gamedata {

struct Polygon {
  std::vector<glm::vec3> pts;  // convex, in order
  glm::vec3 color;             // 0..1
  uint8_t type = 0, colorIndex = 0;  // as in the display list
  // Seen from one side only: its vertices go round clockwise seen from the front (against its
  // stored normal), as with every polygon that has a normal. A thin part (a wing) is two such
  // polygons back to back, each with its own colour; the others are drawn from both sides.
  bool oneSided = false;
  // Painted with a texture of OBJTEXT.BIN (the camouflage of the aircraft): its number and the
  // texel each corner falls on (0..63); -1: the plain colour.
  int texture = -1;
  std::vector<glm::vec2> uv;
};

// Model in game body axes (x right, y up, nose to -z), metres, origin on the ground.
struct Model {
  std::string name;
  std::vector<Polygon> polys;
  glm::vec3 lo{0.0f}, hi{0.0f};  // bounding box
};

// Size of one vertex unit, calibrated on the vehicles (BMP-2 6.7 m, T-62 6.6 m long).
constexpr float kMetresPerUnit = 0.019f;

class OriginalData {
public:
  // `dir` is the game directory (the one with HIND.EXE); file names are matched ignoring case.
  bool open(const std::string& dir);
  bool loaded() const { return !objs_.empty(); }
  const std::string& error() const { return error_; }

  int count() const { return int(offsets_.size()) - 1; }
  std::string name(int index) const;
  // Most detailed object with this long name (e.g. "BRDM_2_APC"), or -1.
  int find(const std::string& name) const;
  // The object with its sub-objects (wheels, turrets) put in place, but for those named in `skip`.
  Model model(int index, const std::vector<std::string>& skip = {}) const;
  // The sub-objects placed directly in this one and where (game axes, metres): the joints of a
  // man's limbs, the doors of a helicopter.
  struct Part {
    int object;
    glm::vec3 at;
  };
  std::vector<Part> parts(int index) const;
  // The most detailed version of the object that decodes cleanly: some aircraft come out
  // garbled at full detail but well at the next level.
  int cleanest(int index) const;
  // The textures of the objects (3DDATA/OBJTEXT.BIN): 64 x 64 texels each, RGBA, one after
  // another; none when the file is missing.
  static constexpr int kTextureSize = 64;
  int textureCount() const { return int(textures_.size() / (kTextureSize * kTextureSize * 4)); }
  const std::vector<uint8_t>& textures() const { return textures_; }
  // Polygons that could not be decoded in the last model() call (for diagnostics).
  int skipped() const { return skipped_; }

private:
  void gather(int index, glm::ivec3 offset, int depth, Model& out, const std::vector<std::string>& skip) const;
  static void liftDecals(Model& m);

  std::vector<uint8_t> objs_;
  std::vector<uint32_t> offsets_;  // [1..N]
  std::vector<glm::vec3> palette_;
  std::vector<uint8_t> textures_;
  std::string error_;
  mutable int skipped_ = 0;
};

// `relative` (e.g. "COCKPITS/PILOT.LBM") under the game directory, each part matched ignoring
// case; empty when it does not exist.
std::string findOriginalFile(const std::string& dir, const std::string& relative);

// The configured directory if it holds the game, otherwise the usual places of a DOSBox
// installation (~/dosbox/c/DI/HIND and the like). Empty when nothing is found.
std::string findOriginalDir(const std::string& configured);

}  // namespace gamedata
