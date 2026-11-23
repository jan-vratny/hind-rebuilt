#pragma once
#include <vector>

#include "gamedata/original.hpp"
#include "render/mesh.hpp"

// The original game's Mi-24 (3DOBJS.BIN), put together from its parts as the game does: the
// player's Mi-24V with the lower nose and the lower rear fuselage for the gear up or down and
// the chin turret apart (it turns), and the simpler model of the other Hinds. The rotors are
// left out, ours turn instead. Axes and origin as our own model's (models.hpp).
namespace render {

// The original's gear stands longer than ours: the model is lifted to put its wheels on the ground.
constexpr float kOriginalMi24Lift = 0.27f;

struct OriginalMi24 {
  MeshBuilder body, gearUp, gearDown, turret;
  // The cabin doors (upper and lower halves each side) shut, and open: the upper halves up, the
  // lower ones down as a step.
  MeshBuilder doorsShut, doorsOpen;
  // With door guns, the doors open: the body with its doorways cut out of its sides, the cabin
  // seen through them (grey-green walls, the troops' brown seats) and a gunner sitting at each
  // door's gun (the original's soldier; side 0 left, 1 right).
  MeshBuilder bodyOpen, cabin, gunner[2];
  // Where a door gun turns in the original's doorway, on its post by the floor.
  static glm::vec3 doorGunPivot(int side);
  // The pieces it breaks into when it crashes (sim::Debris): the middle of the hull with the
  // gear and the turret, then the parts of the original's model (nose halves, rear fuselage,
  // tail boom, tail, wings), each about its own centre (pieceCentres, body axes), with a size.
  std::vector<MeshBuilder> pieces;
  std::vector<glm::vec3> pieceCentres;
  std::vector<float> pieceRadii;
  // False (and nothing built) without the original's data.
  bool build(const gamedata::OriginalData& data);
};

// Another Hind: on the ground with the gear down (origin on the ground), and in the air with the
// gear up (origin where ours has it, for the rotor at kMainRotorHub).
bool buildOriginalWingman(const gamedata::OriginalData& data, MeshBuilder& parked, MeshBuilder& flying);

}  // namespace render
