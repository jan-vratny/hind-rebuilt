#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "net/wire.hpp"
#include "sim/bowman.hpp"
#include "sim/enemy.hpp"
#include "sim/heli.hpp"
#include "sim/mission.hpp"
#include "sim/spotter.hpp"
#include "sim/targets.hpp"
#include "sim/troops.hpp"
#include "sim/weapons.hpp"
#include "sim/wing_arms.hpp"
#include "sim/wso.hpp"

// The network game: the pilot's game flies the mission (the helicopter, the weapons, the enemy,
// everything), the gunner's only shows it. The pilot's sends what the gunner's needs to draw the
// world and make its sounds; the gunner's sends back what its player does with the gun.
namespace net {

enum Message : uint8_t {
  Start = 1,     // pilot -> gunner: the flight starts (again): StartInfo
  Wait = 2,      // pilot -> gunner: no flight now (the pilot plans); the lines to show
  Snapshot = 3,  // pilot -> gunner: the state of the flight (Mirror::write)
  Input = 4,     // gunner -> pilot: what the gunner does
  Wingman = 5,   // wingman -> pilot: his Hind as he flies it, his rounds in the air, what they hit
  Hello = 6,     // the other player -> pilot: his name and his seat (main's --view), on joining and on changing seats
  Chat = 7,      // a message between players: its number (chatText), who sent it
  Bowman = 8,    // the lone bowman -> pilot: where he is (BowmanPose), where his arrows went off
};

// The messages players send each other (Ctrl+1..0), as the original's two-player game had them,
// each with its recording (gamedata::snd::TplrFirst + number - 1): 1..10 the gunner to his pilot,
// 11..20 the pilot to his crew, 21..30 between the pilots of the game's Hinds.
const char* chatText(int number);

// The second player in a Hind of his own: his game flies it and fires its weapons at the
// battlefield as the snapshots show it; the pilot's game draws it and applies what the rounds hit.
struct WingmanReport {
  std::vector<sim::Projectile> shots;  // his rounds and rockets in the air
  std::vector<sim::Impact> hits;       // where they came down since the last report (target: the unit hit)
};

// What the gunner's game sets its flight up from.
struct StartInfo {
  uint32_t generation = 0;  // counts the starts, so a restart is told from a snapshot of the old flight
  std::string zone;         // world.zone key
  std::string mission;      // the mission's file name, or "" for the free flight
  sim::Loadout loadout;
  glm::vec2 home{0.0f};     // the take-off point (the HUD's home)
  float heading = 0.0f;
  int flightModel = 1;
  float hour = 10.0f;       // free flight: the time of day and the weather
  int weather = 0;
  std::vector<glm::vec2> route;  // the player's route as planned
  std::vector<float> routeAlts;
  uint32_t targets = 0;     // units on the battlefield, to check both have the same
  uint32_t slot = 0;        // a wingman's place in the row beside the pilot's Hind
  std::string game;         // the game's name (a wingman's game says it on the network)
  uint8_t infiniteWeapons = 0, doorReloads = 0, noCrash = 0;  // Quickstart's: the wingmen's Hinds the same as the pilot's
  // Quickstart over Ukraine: the flight starts in the air (at `home`, `heading`, this high above
  // the ground), the fuel not used up.
  uint8_t airStart = 0, infiniteFuel = 0;
  float airAltitude = 0.0f;
  void write(Writer& w) const;
  bool read(Reader& r);
};

// What happened in the pilot's game since the last snapshot: for the effects and the sounds.
struct Events {
  std::vector<sim::Impact> impacts;
  std::vector<sim::EnemyEvent> enemy;
  std::vector<sim::WeaponType> fired;
  std::vector<int> radio, effects;  // gamedata::snd lines and sounds of the helicopter
  void clear() { impacts.clear(), enemy.clear(), fired.clear(), radio.clear(), effects.clear(); }
};

// What the gunner does, from his game each frame (Input, followed by his actions).
struct GunnerInput {
  uint8_t seat = 0;     // 1: in the gunner's seat, where his sight aims the turret
  uint8_t trigger = 0;  // his fire button
  uint8_t armed = 0;    // his weapon chosen (else safe)
  uint8_t aiming = 0;   // he turned the sight by hand since the last (the silicon WSO lets go)
  sim::WeaponType weapon = sim::WeaponType::YakB;
  float az = 0.0f, el = 0.0f;           // the sight, relative to the airframe (+ left, + up)
  glm::vec3 dirBody{0.0f, 0.0f, -1.0f}; // the line of sight in the helicopter's axes
  // At a door: interceptor drones sent out and let go (counted: each new one is one more), and
  // how he steers the one he flies (-1..1, x left, y up); locked on a target (counted, the
  // last on lockTarget, an index into the targets).
  uint8_t launches = 0, releases = 0, locks = 0;
  int16_t lockTarget = -1;
  glm::vec2 steer{0.0f};
};
// His keys that work in the pilot's game: the silicon WSO on and off (S), another target (B),
// the target along the line of sight (T), the door gunners free or holding fire (D).
enum class GunnerAction : uint8_t { Wso = 1, Rescan, Track, DoorGunners };

// The game's state that the snapshots carry.
struct World {
  sim::Helicopter& heli;
  sim::Weapons& weapons;
  sim::Targets& targets;
  sim::EnemyFire& enemy;
  sim::Troops& troops;
  sim::Wso& wso;
  sim::Spotter& spotter;
  sim::MissionState& mission;
};

// Another Hind of the game as the others draw it (a wingman's; the host's to a wingman's crew).
struct HindPose {
  glm::vec3 pos{0.0f};
  glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
  float rotorAngle = 0.0f, tailRotorAngle = 0.0f, gearPos = 1.0f, rotorRpm = 0.0f;
  uint32_t crashed = 0;
  int8_t id = -1;  // as sim::Weapons::OtherHind: whose it is, for what our rounds hit
  static HindPose of(const sim::Helicopter& h);
  void apply(sim::Helicopter& h) const;
};

// The lone bowman as the others draw him (sim::Bowman): the man, and his arrow in the air.
struct BowmanPose {
  glm::vec3 pos{0.0f};
  float yaw = 0.0f, pitch = 0.0f, stride = 0.0f, draw = 0.0f;
  uint8_t walking = 0, flying = 0;
  uint8_t weapon = 0, dead = 0;  // in his hands (sim::Bowman::Weapon: the bow, the M60, the LAW); lying dead
  glm::vec3 arrowPos{0.0f}, arrowVel{0.0f};
  // His LAW locked on a Hind (its id), its rocket flying at one (still after it); -1 none.
  int8_t lockHind = -1, rocketHind = -1;
  static BowmanPose of(const sim::Bowman& b);
};

// The rest of the frame: the clock, the pause, the pilot's controls (the HUD shows them), the
// page over the game (the debriefing).
struct Frame {
  double simTime = 0.0;
  bool paused = false;
  sim::Controls controls;
  bool cyclicKeys = false;
  uint8_t doorsTaken = 0;  // the doors the other players of the crew are at: 1 left, 2 right
  // The pilot's game decides who has a seat: the one who came to it first. To the other one at a
  // door: over to the other door (1 left, 2 right); 3: no seat for him, back to the menu.
  uint8_t doorMove = 0;
  std::vector<std::string> page;
  // The wingman's Hind as the enemy's fire left it (the pilot's game counts the hits), and
  // what it last told him (a missile launched at him, a system failing).
  sim::Helicopter::Damage wingmanDamage;
  std::string wingmanMessage;
  int wingmanFlares = 0, wingmanChaff = 0;  // his cartridges left
  // The game's other Hinds (not the one the snapshot is of, nor the receiver's own).
  std::vector<HindPose> others;
  std::vector<BowmanPose> bowmen;  // the lone bowmen about (not the receiver himself)
  // The AI Hinds' stores and rounds in the air (sim::WingArms: the pilot's game flies them).
  std::vector<sim::WingArms::Stores> wingStores;
  std::vector<sim::Projectile> wingShots;
};

class Mirror {
public:
  // A new flight: the units go in full again.
  void reset() { sent_.clear(); }
  // A snapshot; the units only when they changed since the last one sent.
  void write(Writer& w, uint32_t generation, const World& world, const Frame& frame, const Events& events);
  // Reads one into the gunner's game (false: damaged, nothing to rely on).
  static bool read(Reader& r, uint32_t& generation, World& world, Frame& frame, Events& events);
  // Between snapshots: things move on as they were moving.
  static void advance(World& world, float dt);
  // The planned route, both ways.
  static void route(const sim::MissionState& m, std::vector<glm::vec2>& route, std::vector<float>& alts);
  static void setRoute(sim::MissionState& m, const std::vector<glm::vec2>& route, const std::vector<float>& alts);
  // A helicopter's state, both ways (the snapshots' and the wingman's).
  static void writeHeli(Writer& w, const sim::Helicopter& h);
  static void readHeli(Reader& r, sim::Helicopter& h);

private:
  struct Unit {
    glm::vec3 pos, vel;
    float heading, health, delay;
    uint32_t next;
    uint8_t destroyed, aboard, struck, hover;
  };
  static Unit unitOf(const sim::Vehicle& v);
  std::vector<Unit> sent_;
  size_t mines_ = size_t(-1);
};

}  // namespace net
