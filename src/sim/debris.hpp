#pragma once
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "world/terrain.hpp"

// The helicopter breaking up when it crashes, as the original's does: its parts (the nose
// halves, the rear fuselage, the tail boom, the tail, the wings, the rotors...) are thrown apart
// by the blast with the speed it hit at, tumble, bounce along the ground and come to rest.
namespace sim {

struct DebrisPiece {
  glm::vec3 pos{0.0f};    // its centre, world
  glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 vel{0.0f}, omega{0.0f};  // m/s, rad/s (world axes)
  float radius = 0.5f;    // m, how far its centre stays above the ground
  bool resting = false;
};

class Debris {
public:
  // Breaks up a helicopter at `at` with `orient`, moving with `vel`: the pieces' centres in its
  // body axes, their sizes (radius, m).
  void start(glm::vec3 at, glm::quat orient, glm::vec3 vel, const std::vector<glm::vec3>& centres,
             const std::vector<float>& radii);
  void reset() { pieces_.clear(); }
  void step(const world::Terrain& terrain, float dt);
  bool active() const { return !pieces_.empty(); }
  const std::vector<DebrisPiece>& pieces() const { return pieces_; }

private:
  float random();  // -1..1
  std::vector<DebrisPiece> pieces_;
  unsigned rng_ = 4711u;
};

}  // namespace sim
