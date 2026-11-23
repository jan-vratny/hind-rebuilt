#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gamedata/mission.hpp"
#include "gamedata/original.hpp"
#include "gamedata/zone.hpp"
#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "sim/troops.hpp"
#include "sim/weapons.hpp"

namespace net {
class Mirror;
}

// A mission of the original game in play: its forces put on the battlefield, the player's route
// to fly, and the result. The war zone's point `origin` (m east and north) is the world origin.
namespace sim {

// The type of an object of the original game (3DOBJS.BIN): what it is, how hard to destroy,
// its hit box from the model. Registered on first use.
VehicleKind unitKind(const gamedata::OriginalData& original, int object);

// The stores the mission loads (gamedata::Mission::weapons); false if it says none.
bool missionLoadout(const gamedata::Mission& mission, Loadout& out);

// The war zone's buildings, bridges, copses and scrub (gamedata::Zone::objects), with the
// zone's point `origin` at the world origin, as scenery among the targets.
void addScenery(const std::vector<gamedata::ZoneObject>& objects, const gamedata::OriginalData& original, glm::vec2 origin,
                Targets& targets);

// What the player's target waypoints are, as the original names them in the waypoint's
// information ("Waypoint X (Government Building)"): the nearest building of the zone or unit of
// the mission within 200 m that the WSO has a word for; "" for the other waypoints.
std::vector<std::string> waypointTargets(const gamedata::Mission& mission, const gamedata::OriginalData& original,
                                         const std::vector<gamedata::ZoneObject>& zoneObjects);

// Aborted: the player ended it before its end (Ctrl+Q, as in the original).
enum class MissionResult { Flying, Failed, Success, Outstanding, Aborted };

class MissionState {
public:
  friend class net::Mirror;  // copies the state to the gunner's game
  // Puts the mission's forces into `targets` (cleared first), among the war zone's `scenery` if
  // given, some of it in ruins as the mission has it. Without the original's models the units get
  // boxes of a plausible size.
  void start(const gamedata::Mission& mission, const std::vector<gamedata::Formation>& formations,
             const gamedata::OriginalData& original, glm::vec2 origin, Targets& targets,
             const std::vector<gamedata::ZoneObject>* scenery = nullptr);
  bool active() const { return active_; }
  const gamedata::Mission& mission() const { return mission_; }

  // Counts the kills and losses, moves the player's steering on as he passes the waypoints, and
  // ends the mission when he lands back home after it or is shot down.
  void step(Helicopter& heli, const Targets& targets, float dt);

  // The player's route in world x, z: the departure (home) first.
  const std::vector<glm::vec2>& route() const { return route_; }
  // The waypoint steered to (1.. route().size() - 1, then 0: home).
  int waypoint() const { return waypoint_; }
  glm::vec2 steerPoint() const { return route_.empty() ? glm::vec2(0.0f) : route_[size_t(waypoint_)]; }
  // The height above the ground planned at it, m.
  float steerAltitude() const { return size_t(waypoint_) < routeAlts_.size() ? routeAlts_[size_t(waypoint_)] : 30.0f; }
  // Letter of a waypoint as in the original's map: A is the departure.
  static char letter(int i) { return char('A' + i); }
  void nextWaypoint(Helicopter& heli, int step = 1);

  // Reconnaissance (R): the data is sent over a few seconds; it is good when the helicopter stays
  // within sight of a reconnaissance waypoint (X, Y in the plan) not yet reported. The result
  // comes back through reconReport() once.
  static constexpr float kReconTime = 4.0f, kReconRange = 1500.0f;
  void startRecon(Helicopter& heli);
  bool reconActive() const { return reconLeft_ > 0.0f; }
  // 1 good, -1 bad, 0 nothing new.
  int reconReport();
  int reconDone() const { return reconDone_; }
  int reconTotal() const { return reconTotal_; }

  // Mine laying (KMGU-2): a compartment let go near a mine-laying waypoint of the plan (type 17,
  // X, Y... in the briefing) not yet mined, low (30-200 m) and fast (150 km/h or more) as the
  // drills say, lays that field. 1 good, -1 off target.
  static constexpr float kMineReach = 300.0f;
  int mineRelease(const MineRelease& r);
  int minesDone() const { return minesDone_; }
  int minesTotal() const { return minesTotal_; }

  // Remote bombing: an allied bomber (task type 18, the MiG-27) drops laser-guided bombs on its
  // target point (the player's X) while the player keeps the target there designated with the
  // laser. Each frame: whether the laser has a target and where. The bomber talks on the radio
  // (lines of the original, gamedata::snd), the bombs come down at the points returned.
  static constexpr float kLaserReach = 250.0f, kBombFall = 30.0f;
  void laser(bool lasing, glm::vec3 spot);
  std::vector<int> takeRadio();
  std::vector<glm::vec3> takeBombs();
  int laserDone() const { return laserDone_; }

  // Whether the troops' task (deploying, evacuating; sim::Troops) is still to be done.
  // (`evacuation`: taking them out, else putting them down.)
  void setTroopsTask(bool needed, bool done, bool evacuation = false) { troopsNeeded_ = needed, troopsDone_ = done, troopsOut_ = evacuation; }
  // Who rides in which helicopter (for sim::Troops::start), and the other helicopters carrying
  // troops by their cargo number (index into the targets, -1 none).
  const std::vector<PassengerGroup>& passengers() const { return passengers_; }
  const std::vector<int>& carriers() const { return carriers_; }
  int laserTotal() const { return int(bombers_.size()); }
  // How near a target point the zone's buildings are targets, m.
  static constexpr float kTargetArea = 400.0f;
  // The autorotation drill: the rotor left free at least this high, m; landed this near the base, m.
  static constexpr float kDrillCut = 100.0f, kDrillBase = 500.0f;
  int neededPrimary() const { return neededPrimary_; }
  int neededTotal() const { return neededTotal_; }

  bool finished() const { return finished_; }
  // The network game's wingmen still flying: the mission goes on after our Hind is down until
  // they are down or home too (or Ctrl+Q).
  void othersAloft(bool aloft) { othersAloft_ = aloft; }
  // Within reach of home (the route's start), where landing ends the mission.
  bool atHome(glm::vec3 pos) const { return !route_.empty() && glm::distance(glm::vec2(pos.x, pos.z), route_.front()) < 2000.0f; }
  // Ctrl+Q, "END MISSION? Y/N", Y: over, aborted.
  void abort() { finished_ = aborted_ = true; }
  MissionResult result() const;
  // Counts for the debriefing.
  int successKills() const { return successKills_; }
  int otherKills() const { return outstandingKills_; }
  int losses() const { return failureLosses_; }
  int successTotal() const { return successTotal_; }
  int failureTotal() const { return failureTotal_; }
  int dronesTotal() const { return dronesTotal_; }
  int dronesStruck() const { return struck_; }
  // Gerbera decoys among the drones, and how many of them were shot down (for nothing).
  int decoysTotal() const { return decoysTotal_; }
  int decoysDowned() const { return decoysDowned_; }
  // The network game's wingman (the second player's Hind) came down: said in the debriefing.
  void wingmanLost(const std::string& by) { wingmanLost_ = true, wingmanLostBy_ = by; }
  // The debriefing as the original's window shows it, line by line ("" a blank line): the
  // mission's words for the result; how it was flown and the tasks done, in the original's
  // sentences ("Good flying.", "However, not all waypoint targets were destroyed."; `lost`: how
  // the Hind was lost, "Hind was shot down by ZSU-23-4."); our counts of the targets; then what
  // was destroyed by whom ("Targets destroyed by you:", "  Enemy Targets:", "  3 x T-72").
  std::vector<std::string> debriefing(const Targets& targets, const std::string& lost = {}) const;
  // "SUCCESS", "OUTSTANDING", "FAILURE", "ABORTED" (for the window's title, as the original's).
  static const char* resultName(MissionResult r);

private:
  gamedata::Mission mission_;
  bool aborted_ = false;
  bool active_ = false, finished_ = false, crashed_ = false, announced_ = false, flownOut_ = false;
  std::vector<glm::vec2> route_;
  std::vector<int> routeTypes_;     // MissionWaypoint::type: 6 reconnaissance
  std::vector<bool> reconned_;
  int reconDone_ = 0, reconTotal_ = 0, reconPoint_ = -1, reconReport_ = 0;
  std::vector<bool> mined_;
  int minesDone_ = 0, minesTotal_ = 0;
  float reconLeft_ = 0.0f;
  struct Bomber {
    glm::vec2 point{0.0f};     // where it releases, over the target
    float release = 0.0f;      // s from the start
    int stage = 0;             // 0 on its way, 1 asked for the laser, 2 told it is lit, 3 bombs falling, 4 done
    bool lit = false;          // the target has been lit since it asked
    float impact = 0.0f;
  };
  std::vector<Bomber> bombers_;
  int laserDone_ = 0;
  bool troopsNeeded_ = false, troopsDone_ = false, troopsOut_ = false;
  std::vector<PassengerGroup> passengers_;
  std::vector<int> carriers_;
  std::vector<float> routeAlts_;
  bool lasing_ = false;
  glm::vec3 spot_{0.0f};
  std::vector<int> radio_;
  std::vector<glm::vec3> bombs_;
  int waypoint_ = 0;
  int successKills_ = 0, outstandingKills_ = 0, failureLosses_ = 0;
  int successTotal_ = 0, outstandingTotal_ = 0, failureTotal_ = 0;
  // Kills of primary targets needed for success, and of all the targets (the mission's count
  // takes in the others too: KOREAC14 needs 20 with 6 primary targets).
  int neededPrimary_ = 0, neededTotal_ = 0;
  // Drones (sim::UnitClass::Drone) in the mission, and those that reached their targets: each
  // counts as a loss towards the mission's failure count.
  int dronesTotal_ = 0, struck_ = 0, decoysTotal_ = 0, decoysDowned_ = 0;
  bool wingmanLost_ = false, othersAloft_ = false;
  std::string wingmanLostBy_;
  float flown_ = 0.0f;  // s since the start
  float crashedFor_ = 0.0f;
  // The autorotation drill: how high the rotor was left free (-1: driven), and the landing: 0 none
  // yet, 1 under power or cut too low, 2 unpowered, 3 unpowered at the base.
  float freeFrom_ = -1.0f;
  int drillLanding_ = 0;
};

}  // namespace sim
