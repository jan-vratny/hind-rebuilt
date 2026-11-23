#pragma once
#include <glm/glm.hpp>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

// What the threat warning receiver (the RWR on the pilot's canopy frame) picks up: enemy radars
// that can see the helicopter. Of the exercise vehicles the ZSU-23-4 tracks with its Gun Dish
// radar and the SA-13 ranges with its own; the SA-9 and the rest have none.
namespace sim {

// The classification lights of the original, 1 to 6.
enum class ThreatClass { EarlyWarning = 1, AircraftRadar, AaaRadar, SamRadar, RfMissile, IrMissile };

struct Threat {
  bool active = false;
  ThreatClass cls = ThreatClass::AaaRadar;
  float bearingDeg = 0.0f;  // from the nose, + to the right, -180..180
  float proximity = 0.0f;   // 0 at the edge of the radar's range, 1 on top of it
  int vehicle = -1;
};

// Radar range (m) of a vehicle kind, 0 for none.
float radarRange(VehicleKind k);

// The greatest threat: the nearest enemy radar for its range that has a line of sight to the helicopter.
Threat greatestThreat(const Helicopter& heli, const Targets& targets, const world::Terrain& terrain);

}  // namespace sim
