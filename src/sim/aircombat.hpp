#pragma once
#include <vector>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

// The enemy's armed aircraft coming for our helicopters (sim::EnemyFire fires their guns and
// missiles): a fighter (Arms::Fighter) or a gunship (Arms::Gunship) flying its mission's route
// that sees one of them within its reach leaves the route and attacks: the fighter in passes,
// diving on it from behind and pulling through and away before it turns for the next; the
// gunship closes to its gun's range and stands off there, its turret on it. With nobody left to
// fight it goes back to its route where it left it.
namespace sim {

class AirCombat {
public:
  explicit AirCombat(const world::Terrain& terrain) : terrain_(terrain) {}
  bool enabled = true;
  static constexpr float kFighterReach = 7000.0f, kGunshipReach = 4000.0f;
  void reset() { units_.clear(); }
  // `helis`: ours they may attack (null: none there).
  void step(Targets& targets, const std::vector<const Helicopter*>& helis, float dt);
  // Whether unit i is attacking (for tests and the display).
  bool attacking(int i) const { return i >= 0 && size_t(i) < units_.size() && units_[size_t(i)].engaged; }

private:
  struct Unit {
    bool engaged = false;
    int target = -1;       // in helis
    float look = 0.0f;     // s to the next look round
    float extend = 0.0f;   // a fighter's s of flying on through after a pass
    glm::vec2 away{0.0f};  // its way out
    std::vector<RoutePoint> plan;  // its route as it was
    size_t planNext = 0;
    bool planLoop = false;
  };
  bool sees(glm::vec3 from, glm::vec3 to) const;
  const world::Terrain& terrain_;
  std::vector<Unit> units_;
};

}  // namespace sim
