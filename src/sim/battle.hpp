#pragma once
#include <vector>

#include "sim/enemy.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

// The fighting on the battlefield between the enemy and our units (sim::EnemyFire is the
// enemy's fire at the player's helicopter): an armed unit of either side picks the nearest of the
// other side's it sees within its weapon's reach, columns, helicopters, soldiers, ships, and
// wears it down burst by burst, rocket by rocket. Ours fire back at the enemy as he fires at
// them, though less well (kOurAim): the battle is the player's to win.
// The rounds are not flown one by one: each burst hits with a chance that falls with the range
// (and against an aircraft crossing fast), for damage by the target's armour. Escorted columns
// and Hips are lost so unless the player silences their attackers first. The fighting flares up
// where the player is (within kActiveRange of the helicopter), so the battle they are sent to
// is still going on when they get there. (How fast the original lets them die is not known.)
namespace sim {

class GroundBattle {
public:
  explicit GroundBattle(const world::Terrain& terrain) : terrain_(terrain) {}

  bool enabled = true;
  static constexpr float kActiveRange = 5000.0f;
  static constexpr float kOurAim = 0.6f;  // our units' chance of a hit against the enemy's
  void reset() { crews_.clear(); }
  void seed(unsigned s) { rng_ = s; }
  // Fires (muzzle flashes, launches, bursts at the target) go to `events`, marked as not at the
  // helicopter.
  void step(Targets& targets, glm::vec3 heliPos, float dt, std::vector<EnemyEvent>& events);

private:
  struct Crew {
    int target = -1;      // index into the targets
    float look = 0.0f;    // s to the next choice of target
    float next = 0.0f;    // s to the next burst or launch
    int missiles = -1;    // -1 not yet given
  };
  float random();
  bool lineOfSight(glm::vec3 from, glm::vec3 to) const;

  const world::Terrain& terrain_;
  std::vector<Crew> crews_;
  unsigned rng_ = 1996u;
};

}  // namespace sim
