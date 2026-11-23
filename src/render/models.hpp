#pragma once
#include <glm/glm.hpp>

#include "render/mesh.hpp"
#include "sim/weapons.hpp"

namespace render {

// Body coordinates: x right, y up, z back, origin at the centre of gravity.
// Our own simple Mi-24V, for when the original game's model is not there.
enum class Mi24Parts {
  Exterior,  // airframe without rotors and landing gear
  Parked,    // complete static aircraft with drooped blades
};

constexpr glm::vec3 kMainRotorHub{0.0f, 2.33f, 0.0f};
constexpr float kMainRotorRadius = 8.65f;
constexpr glm::vec3 kTailRotorHub{-0.68f, 2.16f, 10.41f};
constexpr glm::vec3 kPilotEye{0.0f, 0.22f, -3.35f};
constexpr glm::vec3 kGunnerEye{0.0f, -0.35f, -4.60f};

// stores: with the usual load (B-8V20 pods inner and outer, 9M114 tubes on the
// wingtips); without, the pylons are bare and buildStore() hangs what is carried.
void buildMi24(MeshBuilder& mb, const glm::mat4& m, Mi24Parts parts, bool stores = true);
// One store on a pylon, side 0 left, 1 right; a rack of interceptor drones with `rounds` of them
// still on it (-1: full).
void buildStore(MeshBuilder& mb, const glm::mat4& m, sim::Store store, sim::Pylon pylon, int side, int rounds = -1);
// The Yak-B barrels of the chin turret in body coordinates (rotate them about kTurretPivot).
void buildChinGun(MeshBuilder& mb, const glm::mat4& m);
void buildLandingGear(MeshBuilder& mb, const glm::mat4& m);
// Rotor meshes in hub-local coordinates (main rotor spins about y, tail rotor about x).
void buildMainRotor(MeshBuilder& mb, const glm::mat4& m, float droop);
void buildMainRotorDisc(MeshBuilder& mb);
void buildTailRotor(MeshBuilder& mb, const glm::mat4& m);
// The cabin around a door gunner (side 0 left, 1 right), in body coordinates: the cabin's side
// with the window he fires out of, its floor, roof and ends, and his GShG-7.62 on its post,
// pointing along `gunDir` (body axes).
void buildDoorView(MeshBuilder& mb, int side, glm::vec3 gunDir);
// Only the GShG-7.62 on its post, as seen from outside.
void buildDoorGun(MeshBuilder& mb, int side, glm::vec3 gunDir);
// The same gun turning about `pivot` (airframe axes), with its post to the floor or without it,
// with its ammunition box on the floor by the post or without it.
void buildDoorGun(MeshBuilder& mb, int side, glm::vec3 gunDir, glm::vec3 pivot, bool post, bool ammoBox = true);

}  // namespace render
