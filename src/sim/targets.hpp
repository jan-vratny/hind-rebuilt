#pragma once
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "world/terrain.hpp"

// Units on the battlefield that projectiles can hit and destroy: vehicles, soldiers, guns,
// tents and stores, parked and flying aircraft. They stand still or follow a route.
// World axes as in terrain.hpp; a unit's own axes are x right, y up, nose to -z.
namespace sim {

enum class WeaponType;

// The exercise's vehicles have fixed kinds; the types of a mission's units are registered as
// they are needed (registerVehicleType) and get the kinds from Count on.
enum class VehicleKind : int { Brdm2, Bmp2, Zsu23, Sa9, Sa13, T62, FuelTruck, Shahed, Gerbera, Geran3, Banderol, Count };
// A decoy: the Gerbera drone, looking and flying like a Shahed to draw the fire, with no
// warhead (it does nothing where it comes down, and downing it scores nothing).
inline bool isDecoy(VehicleKind k) { return k == VehicleKind::Gerbera; }
// The jet one: the Geran-3 (Shahed-238), with a turbojet in place of the piston engine.
inline bool isJetDrone(VehicleKind k) { return k == VehicleKind::Geran3 || k == VehicleKind::Banderol; }
// The S8000 Banderol: a jet cruise missile at some 500 km/h, too fast for an interceptor drone
// (the R-60 is what catches it).
inline bool isCruiseMissile(VehicleKind k) { return k == VehicleKind::Banderol; }

enum class Armour { Soft, Light, Heavy };

// Drone: a loitering munition (the Shahed-136, the Geran-3), flying its route to dive onto the target at its end.
enum class UnitClass { Vehicle, Soldier, Aircraft, Helicopter, Structure, Drone };

// What a unit can shoot at the helicopter with: a rifle (AK-47), a heavy machine gun (DShK,
// KPVT), a grenade launcher (RPG-7), a shoulder launched missile (Strela-2), anti-aircraft
// guns (ZSU-23-4, the Oerlikon GDF), a missile system (Rapier, Strela-1, Strela-10); a tank's
// gun (at units on the ground only); a gunship's cannon and air-to-air missiles (AH-64, AH-1,
// Mi-24); a fighter's cannon and heat-seeking missiles (MiG-29, F-16, Su-25).
enum class Arms { None, Rifle, Hmg, Rpg, Manpads, Aaa, Sam, TankGun, Gunship, Fighter };

// A flat convex face of a unit's model, in its axes: what a man on foot walks on and into.
struct ShapeFace {
  std::vector<glm::vec3> pts;
  glm::vec3 normal{0.0f, 1.0f, 0.0f};  // unit
};

struct VehicleType {
  std::string name;      // shown when it is destroyed
  std::string original;  // object name in the original game's 3DOBJS.BIN
  Armour armour = Armour::Light;
  float health = 12.0f;
  glm::vec3 lo{-1.0f, 0.0f, -1.0f}, hi{1.0f, 2.0f, 1.0f};  // hit box in vehicle axes, metres
  UnitClass cls = UnitClass::Vehicle;
  int radarClass = 0;       // threat warning light (see threats.hpp), 0 for no radar
  float radarRange = 0.0f;  // m
  int object = -1;          // its model in 3DOBJS.BIN; -1: the first object of that name
  Arms arms = Arms::None;
  std::vector<ShapeFace> faces;  // the original's buildings, bridges and the like: their real shape
};
const VehicleType& vehicleType(VehicleKind k);
int vehicleTypeCount();
// The kind with this original object name, or Count when none is registered.
VehicleKind findVehicleType(const std::string& original);
// Registers a type (or returns the kind already registered under its original name).
VehicleKind registerVehicleType(const VehicleType& t);

enum class Side { Neutral, Ours, Enemy };
// What its destruction means for the mission (gamedata::Kill).
enum class Objective { None, Success, Outstanding, Failure };

// Who destroyed a unit, for the debriefing (as the original's): the player's Hind (its crew,
// their drones and mines), one of our other Hinds (named: the AI wingman, a player of the network
// game), the other allied forces, the enemy's; None for one in ruins from the start.
enum class Killer : uint8_t { None, Player, Wingman, Allied, Enemy };

// A point of a unit's route in world coordinates.
struct RoutePoint {
  glm::vec2 pos{0.0f};  // world x, z
  float alt = 0.0f;     // m above the ground (aircraft)
  float speed = 0.0f;   // m/s on the leg to here
  float wait = 0.0f;    // s spent here
};

struct Vehicle {
  VehicleKind kind;
  glm::vec3 pos;          // on the ground (aircraft: in the air)
  float heading = 0.0f;   // rad, 0 = nose north (-z), clockwise seen from above
  glm::vec3 vel{0.0f};    // m/s
  float health = 1.0f;
  bool destroyed = false;
  float sinceDestroyed = 0.0f;  // s, for the fire and smoke of the wreck
  Side side = Side::Enemy;
  Objective objective = Objective::None;
  bool armed = true;  // false: the mission's "unarmed" units do not shoot
  bool aboard = false;  // taken into the helicopter's cabin: no longer on the battlefield
  bool scenery = false; // a building, bridge or scrub of the war zone, not a unit of the mission
  bool struck = false;  // a drone that reached its target and blew up there (not shot down)
  Killer killer = Killer::None;  // who destroyed it
  std::string killerName;        // the Hind's of Killer::Wingman
  bool spent = false;   // a decoy that came down at the end of its flight (not shot down)
  float flown = 0.0f;   // m a drone has flown (its weaving and its height go by it)
  bool hover = false;   // a helicopter holding in the air (sim::Orders, or waiting up there to start its route), drawn flying when it stands still
  bool brokenUp = false;  // in pieces now (main's wrecks): this one no longer drawn
  bool turretOff = false; // a Russian tank's turret blown off (main's wrecks): its hull stays, burnt out
  // Movement: from route[0] towards route[next], after `delay` seconds; looping routes start
  // over at the end, the others stop there.
  std::vector<RoutePoint> route;
  size_t next = 0;
  bool loop = false;
  float delay = 0.0f, waiting = 0.0f;
  bool moving() const { return !destroyed && !aboard && next < route.size() && (next > 0 || loop); }
  // Vehicle axes to world.
  glm::mat4 toWorld() const;
  // A drone shot down and not yet on the ground (it falls spinning).
  bool fallingDrone() const;
};

class Targets {
public:
  explicit Targets(const world::Terrain& terrain) : terrain_(terrain) {}

  // Puts a vehicle on the ground at (x, z).
  int add(VehicleKind kind, float x, float z, float headingRad);
  // Gives vehicle i a route to follow, its first point being where it is. Aircraft fly at the
  // points' heights above the ground, the rest drive or walk on it.
  // It may start further along, at point `start` (heading to the next one).
  void setRoute(int i, std::vector<RoutePoint> route, bool loop, float delay, size_t start = 0);
  // The exercise area north of the airfield: a convoy with its air defence, and a tank platoon.
  void placeExercise();
  void clear() { list.clear(); }

  // First vehicle the segment a-b passes through: its index and the hit point, or -1.
  int raycast(glm::vec3 a, glm::vec3 b, glm::vec3& hit) const;
  // A unit in the air (a helicopter, an aircraft, a drone) that one of `points` (world) is in,
  // its box grown by its rotor's reach for a helicopter: a mid-air collision. -1: none.
  int airCollision(const std::vector<glm::vec3>& points) const;
  // A round of `weapon` hits vehicle i (or the ground nearby for i < 0, rockets only), fired
  // by `by` (a wingman's: named `name`). Returns the indices of the vehicles this destroyed.
  std::vector<int> damage(int i, WeaponType weapon, glm::vec3 at, Killer by = Killer::Player, const std::string& name = {});
  // Points of damage to vehicle i from the fire of the battle on the ground (sim::GroundBattle),
  // of `by`; whether it was destroyed by it.
  bool harm(int i, float points, Killer by);
  // Marks unit i destroyed by `by` (unless it already is).
  void destroy(int i, Killer by, const std::string& name = {});

  // Wrecks burn, units move along their routes; an enemy on the ground walking or driving onto a
  // mine is blown up (see takeMineKills).
  void step(float dt);

  // A mine of a KMGU-2 lying on the ground.
  void addMine(glm::vec3 at) { mines.push_back(at); }
  std::vector<glm::vec3> mines;
  // The units the mines destroyed since last asked.
  std::vector<int> takeMineKills();
  // Where drones dived onto their targets since last asked (their blast has done its damage).
  std::vector<glm::vec3> takeStrikes() { return std::exchange(strikes_, {}); }
  // A drone's warhead: Shahed-136, some 50 kg.
  static constexpr float kStrikeRadius = 25.0f;
  int alive() const;

  std::vector<Vehicle> list;

private:
  void stepUnit(Vehicle& v, float dt);
  std::vector<int> mineKills_;
  std::vector<glm::vec3> strikes_;
  const world::Terrain& terrain_;
};

}  // namespace sim
