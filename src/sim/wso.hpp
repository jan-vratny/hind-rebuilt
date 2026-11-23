#pragma once
#include <vector>

#include <glm/glm.hpp>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "sim/weapons.hpp"
#include "world/terrain.hpp"

namespace net {
class Mirror;
}

// The "Silicon WSO" of the original: the computer weapon systems operator in the front seat.
// While he is on, he picks a target ahead, tracks it and lays the turret gun on it with lead;
// the pilot only selects the weapon and fires. Keys as in the original: S on/off, B rescan for
// another target, T track the target along the line of sight. Steering the gunner's view by
// hand switches him off.
namespace sim {

class Wso {
public:
  friend class net::Mirror;  // copies the state to the gunner's game
  static constexpr float kSearchRange = 4000.0f;  // m
  static constexpr float kSearchAngle = 1.05f;    // rad either side of the nose
  static constexpr float kLaserAngle = 0.14f;     // rad: designator field (about the HUD width)

  explicit Wso(const world::Terrain& terrain) : terrain_(terrain) {}

  bool enabled() const { return enabled_; }
  void setEnabled(bool on, Helicopter& heli);
  void toggle(Helicopter& heli) { setEnabled(!enabled_, heli); }
  void reset() { target_ = -1, searchTimer_ = 0; }
  // He takes our own forces for targets too (an option of the mission list).
  bool allies = false;
  // The R-60 selected: he looks for fliers first, and for them out to its reach (Weapons::kR60Range).
  bool airFirst = false;

  // Another target than the current one (the next in his order of preference).
  void rescan(Helicopter& heli, const Targets& targets);
  // The live target closest to the line of sight from `eye` along `dir` (world), within a few degrees.
  void track(Helicopter& heli, const Targets& targets, glm::vec3 eye, glm::vec3 dir);
  // A target someone else points out (the Mi-17's call): taken if it is in his reach and sight,
  // without a word (the call is on the display already).
  bool designate(Helicopter& heli, const Targets& targets, int k);
  // Keeps the target valid, searches for one when he is on, and aims the turret at it.
  void update(Helicopter& heli, Weapons& weapons, const Targets& targets, float dt);

  int target() const { return target_; }
  // Where to aim at the target (world), and whether the laser can range it (target near the nose).
  glm::vec3 aimPoint(const Targets& targets) const;
  bool lasing(const Helicopter& heli, const Targets& targets) const;
  float range(const Helicopter& heli, const Targets& targets) const;

private:
  // Live, visible targets ahead, best first.
  std::vector<int> candidates(const Helicopter& heli, const Targets& targets) const;
  bool visible(glm::vec3 from, glm::vec3 to) const;
  float reach(const Vehicle& v) const;  // how far he looks for that one

  const world::Terrain& terrain_;
  bool enabled_ = true;
  int target_ = -1;
  float searchTimer_ = 0.0f;
};

}  // namespace sim
