#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "sim/weapons.hpp"
#include "world/terrain.hpp"

// The AI Hinds of our flight over Ukraine carry what the player does not: with R-60s on the
// player's outer pylons they all have the interceptor drones' rack, else the wingman (the first
// of them) has the R-60s for the fast ones (a Banderol, a Geran-3 in need) and the others the
// interceptors. Each has its own Weapons on a stand-in Helicopter put where the unit flies, so
// its rounds fly, hit and are drawn as the player's do; the kills are the wingman's.
namespace sim {

class WingArms {
public:
  explicit WingArms(const world::Terrain& terrain) : terrain_(terrain) {}
  // Arms our AI Mi-24s of `targets` against what `player` carries; `infinite`/`r60Reloads` as
  // the player's weapons have it (Quickstart).
  void arm(const Targets& targets, const Loadout& player, bool infinite, bool r60Reloads);
  void clear() { hinds_.clear(); }
  // Puts each where its unit is, lets the wingman fire an R-60 at a fast drone it can lock on
  // (none already after it, `player`'s rounds too), and flies their rounds.
  void step(Targets& targets, const Weapons* player, float dt, std::vector<Impact>& impacts);

  struct Hind {
    int index = -1;  // the unit in the targets
    std::unique_ptr<Helicopter> heli;
    std::unique_ptr<Weapons> weapons;
    float look = 0.0f;  // s to its next look for a target
  };
  const std::vector<Hind>& hinds() const { return hinds_; }
  // What hangs under each one's wings now, for drawing it (and for the network game's other
  // players, whose games run none of this).
  struct Stores {
    int16_t index = -1;                           // the unit in the targets
    uint8_t store[kPylons] = {0, 0, 0};           // sim::Store on each pylon
    int8_t rounds[kPylons][2] = {{0, 0}, {0, 0}, {0, 0}};  // left on it, each side
  };
  std::vector<Stores> stores() const;
  // Their rounds in the air, all together.
  std::vector<Projectile> shots() const;
  // The R-60 wingman looks this far for a fast drone, this far either side of its heading (it
  // turns to it: the unit flies its route, so the launch is towards the drone).
  static constexpr float kR60Reach = 6000.0f, kR60Look = 1.57f;

private:
  const world::Terrain& terrain_;
  std::vector<Hind> hinds_;
};

}  // namespace sim
