#pragma once
#include <string>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

namespace net {
class Mirror;
}

// The designator of a drone-hunting flight: a Mi-17 of ours flying with the player finds the
// enemy's drones (its crew with night sights and a searchlight, the radar on the ground passing
// on what it sees) and calls the one nearest the player out on the radio, where it is from him:
// "HIP: SHAHED 2 O'CLOCK 4.3 KM LOW", kept on the display's upper line and worked out afresh as
// the player and the drone fly. The WSO takes it for his target once it is in his reach.
namespace sim {

class Spotter {
public:
  friend class net::Mirror;  // copies the state to the gunner's game
  static constexpr float kSpotRange = 9000.0f;  // m from the Mi-17

  explicit Spotter(const world::Terrain& terrain) : terrain_(terrain) {}
  void reset() { designated_ = -1, look_ = 0.0f, clear_ = 0.0f; }
  // Whether a Mi-17 of ours is up (alive, in the air) in the mission.
  bool active(const Targets& targets) const;
  void step(const Helicopter& heli, const Targets& targets, float dt);
  // The drone called out (index into the targets), or -1.
  int designated() const { return designated_; }
  // The call for a drone: "SHAHED 2 O'CLOCK 4.3 KM LOW".
  static std::string call(const Helicopter& heli, const Vehicle& drone);
  // The Hip's line on the text display, kept over the other messages: the drone called out,
  // its clock, distance and height worked out afresh each frame ("HIP: SHAHED 2 O'CLOCK 4.3 KM
  // LOW"), for a few seconds "HIP: NO MORE CONTACTS" when the last one is down; else empty.
  std::string line(const Helicopter& heli, const Targets& targets) const;

private:
  bool sees(glm::vec3 from, glm::vec3 to) const;
  const world::Terrain& terrain_;
  int designated_ = -1;
  float look_ = 0.0f, clear_ = 0.0f;
};

}  // namespace sim
