#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

namespace net {
class Mirror;
}

// Troops carried in the cabin, as in the original: land by a landing zone, open the cargo doors
// (D) and the troops get out, or the troops and casualties waiting there come over and climb
// in, two men carrying each stretcher. They walk slowly, and hop down from the cabin floor and
// up into it (it is higher than the ground). Back home the casualties are taken out the same
// way. The flight engineer tells how it went.
//
// Who rides where comes from the mission (gamedata::MissionDrone::cargo / onboard): groups of
// units in a helicopter's cabin from the start, getting out where it lands, and groups waiting
// to be picked up. Those of the other helicopters (wingmen, Hips) get out or in when their
// helicopter sets down by the place.
namespace sim {

// A man on foot about the helicopter.
struct Walker {
  enum class Kind { Soldier, Stretcher } kind = Kind::Soldier;  // Stretcher: two men and a casualty
  glm::vec3 pos{0.0f};
  float heading = 0.0f;     // rad, 0 north (-z), clockwise from above
  float stride = 0.0f;      // walking cycle, rad
  float hop = 0.0f;         // 0..1 through a hop between the cabin floor and the ground; 0 none
  glm::vec3 from{0.0f}, to{0.0f};  // the hop's ends, or where the walk goes (to)
  glm::vec3 dest{0.0f};             // getting out: where the walk ends
  glm::vec3 cabin{0.0f};            // getting in: the cabin floor by the door
  enum class Goal { Out, In, Stand } goal = Goal::Out;
  float wait = 0.0f;        // s before setting off
  bool soldierModel = true; // the deployed troops' model (else the casualty's carriers')
  bool gone = false;        // climbed into the cabin, or become its unit again
  int unit = -1;            // getting out: the unit of the targets it becomes at the end of its walk
  int men = 1;              // getting in: the men it brings aboard (a stretcher: the casualty and carriers)
};

// Units of the targets riding in, or waiting for, one helicopter.
struct PassengerGroup {
  int carrier = 1;          // the helicopter's cargo number: 1 the player's, else carriers[]
  bool out = false;         // in the cabin from the start, to get out at `point`; else waiting there
  glm::vec2 point{0.0f};    // where the helicopter is to land, world x, z
  std::vector<int> units;   // indices into Targets::list
  int men = 0;              // out: men without units of their own (the exercise's troops)
  bool done = false;        // got out, or called over
};

class Troops {
public:
  friend class net::Mirror;  // copies the state to the gunner's game
  // How far from the landing zone the helicopter has to land, m.
  static constexpr float kZoneRadius = 300.0f;
  static constexpr int kCabin = 8;  // troops the cabin takes

  // A mission's passengers: `groups` (their units of `targets` in a cabin are taken off the
  // battlefield), `carriers` the other helicopters by cargo number (index into Targets::list,
  // -1 none); the men the player must put out and take in for success (capped at those there
  // are), and home, where the casualties are taken out.
  void start(std::vector<PassengerGroup> groups, const std::vector<int>& carriers, int deployNeeded, int loadNeeded,
             glm::vec2 home, Targets& targets);
  void reset();

  // D: the doors open or shut.
  void toggleDoors(const Helicopter& heli);
  bool doorsOpen() const { return doors_; }

  void step(const Helicopter& heli, Targets& targets, const world::Terrain& terrain, float dt);

  const std::vector<Walker>& walkers() const { return walkers_; }
  int aboard() const { return troops_ + casualties_ + rescued_; }
  int deployedMen() const { return deployedMen_; }
  int loadedMen() const { return loadedMen_; }
  int deployNeeded() const { return deployNeeded_; }
  int loadNeeded() const { return loadNeeded_; }
  bool deployed() const { return deployedMen_ >= deployNeeded_; }
  bool evacuated() const { return loadedMen_ >= loadNeeded_ && !coming_; }
  bool deployTask() const { return deployNeeded_ > 0; }
  bool evacuationTask() const { return loadNeeded_ > 0; }
  // The landing zones still to go to (the player's groups not yet out or picked up).
  std::vector<glm::vec2> zonesLeft() const;
  // The flight engineer's lines (gamedata::snd) and the sounds of feet, said since last asked.
  std::vector<int> takeRadio();

private:
  glm::vec3 door(const Helicopter& heli, int side, float out) const;
  void addOut(const Helicopter& heli, const world::Terrain& terrain, Walker::Kind kind, bool soldierModel, int unit);
  void addIn(const Helicopter& heli, glm::vec3 from, Walker::Kind kind);
  bool landed(const Helicopter& heli) const;
  void call(PassengerGroup& g, const Helicopter& heli, Targets& targets);
  void stepCarriers(Targets& targets, const world::Terrain& terrain);

  bool doors_ = false;
  int deployNeeded_ = 0, loadNeeded_ = 0, deployedMen_ = 0, loadedMen_ = 0;
  glm::vec2 home_{0.0f};
  std::vector<PassengerGroup> groups_;
  std::vector<int> carriers_;
  // In the cabin: troops to put out (their units in order, -1 for those without), casualties
  // and other men taken in.
  std::vector<int> outQueue_;
  int troops_ = 0, casualties_ = 0, rescued_ = 0;
  std::vector<Walker> walkers_;
  float nextOut_ = 0.0f;
  int outSide_ = 0;
  bool wrongSaid_ = false, coming_ = false;
  std::vector<int> radio_;
};

}  // namespace sim
