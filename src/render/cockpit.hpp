#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render/mesh.hpp"
#include "sim/heli.hpp"

namespace render {

// Live values shown by the cockpit instruments.
struct InstrumentData {
  float mapLight = 1.0f;     // the moving map's picture lit (render::Palette::mapLight)
  glm::vec3 dialLight{1.0f};  // the dials, needles and counters lit (dim at night without the lamps)
  float speedKmh = 0, pitchDeg = 0, rollDeg = 0, headingDeg = 0;
  float verticalSpeed = 0;   // m/s
  float radarAltitude = -1;  // m, negative when out of range (above 700 m)
  float baroAltitude = 0;    // m
  float torque = 0;          // %
  float gLoad = 1;
  float slipDeg = 0;         // sideslip, + air coming from the right
  float yawRateDeg = 0;      // deg/s, + nose right
  float clockSeconds = 0;    // time of day
  bool gearDown = true, gearMoving = false, brakes = false;
  glm::vec2 position{0};     // world x, z (m) for the moving map
  glm::vec2 home{0};         // airfield x, z
  glm::vec2 steer{0};        // where the HSI pointer leads (home or the next waypoint), x, z
  std::vector<glm::vec2> route;  // a mission's waypoints, x, z, for the map
  int waypoint = -1;             // the one steered to
  std::string text;          // text display
  float fuelKg = 0, engineRpm = 0, rotorRpm = 0;  // fuel counter, rpm bars (%)
  // Threat warning receiver: the greatest threat, its class light (1..6), bearing from the nose
  // (deg, + right) and proximity (0..1).
  bool threat = false;
  int threatClass = 0;
  float threatBearing = 0, threatProximity = 0;
  // The WSO's weapon panel: rounds on the inner and outer pylon pairs, the wing tips and in the
  // gun; which of them carry the selected weapon, and its name.
  // Battle damage: failed engines (their rpm bars drop), engine fire, the IR jammer on.
  bool engineFailed[2] = {false, false};
  bool engineWarning[2] = {false, false};
  bool failed[sim::kSystems] = {};  // systems knocked out (sim::System), for the warning lights
  bool fuelDamaged = false;         // a holed tank
  bool fire = false, jammer = false;
  int stationRounds[4] = {0, 0, 0, 0};
  bool stationSelected[4] = {false, false, false, false};
  std::string weapon;
};

// The pilot's (rear) and the weapon systems operator's (front) cockpit in body coordinates,
// rebuilt every frame with the instrument readings. Drawn unlit, like the painted panels of the
// original: the light blue Soviet panel, black dials with white needles.
void buildPilotCockpit(MeshBuilder& mb, const InstrumentData& d);
void buildGunnerCockpit(MeshBuilder& mb, const InstrumentData& d);

}  // namespace render
