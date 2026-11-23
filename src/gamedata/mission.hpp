#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Missions of the original game (MISSDATA), read from the player's own installation:
//  NAME.MIS  game flow, war zone (1 Kazakhstan, 2 Korea, 3 Afghanistan, 4 Saratov), campaign level.
//  NAME.TXT  the briefing: ":Title", ":Description" and ":Para" sections; the first paragraphs
//            are "Key:value" facts (helicopters, targets, weapons, take-off time...).
//  NAME.DTA  the forces: ":Task" groups (":Side" 1 ours, 2 enemy, ":Task Type", ":User Task" for
//            the player's own flight) of ":Drone"s, each one object (":Drone Type" = object of
//            3DDATA/3DOBJS.BIN - 1) or a formation of objects (":Formation Type"), with flags
//            (":Kill Success" / ":Kill Outstanding" / ":Kill Failure", ":Drone unarmed") and a
//            route (":Waypoint" lines, see MissionWaypoint), then ":Gameplay" sections (whose
//            side each 16384 ft cell of the zone is, points where the game puts air defences,
//            tanks and APCs of its own) and the mission's settings (time, weather, wind, the
//            kills needed for success and for an outstanding result).
// Names are e.g. AFGHNC01 (campaign), AFGHNS01 (single mission); C = campaign (24 per zone).
//
// Formations come from HIND.EXE: a table of 141 records of u32 count, then per member int32
// x (right), y (forward), z in 1/256 ft and u32 object - 1, found next to their names.
namespace gamedata {

// One point of a route. Positions are metres east and north in the war zone.
struct MissionWaypoint {
  // 1 departure, 2 turning point, 3 landing, 4 target (at no height: a landing zone),
  // 6 reconnaissance, 17 mine laying; soldiers on foot: 10 walking round in a loop, 12 walking
  // to, 14 boarding a helicopter, 15 in a helicopter's cabin.
  int type = 2;
  glm::vec2 pos{0.0f};
  float alt = 0.0f;     // m above the ground
  float time = 0.0f;    // s from the start to here; < 0: the unit does not move
  float speed = 0.0f;   // m/s on the leg to here
  float wait = 0.0f;    // s spent here (at the first point: delay of the start)
};

enum class Kill { None, Success, Outstanding, Failure };

struct MissionDrone {
  int object = -1;     // 3DOBJS.BIN index, or -1 for a formation
  // Ours (":Drone Name", the remake's missions): the unit by name instead, an object of the
  // original (e.g. "Mi_17_Hip") or a type of the remake's own ("Shahed_136").
  std::string name;
  int formation = -1;  // 1-based, as in the file
  Kill kill = Kill::None;
  bool unarmed = false;
  // Troops carried: a helicopter's ":Drone cargo" number, and the number of the helicopter a unit
  // of soldiers rides in (":Drone onboard"; 1 is the player's). Such a unit's route starts with
  // waypoint 15 (in the cabin, getting out where it lands, then on along the route) or ends with
  // 14 (waiting there to be picked up).
  int cargo = 0, onboard = 0;
  int startWaypoint = 0;  // the route point it starts at (0-based; the route goes on from there)
  // Ours (":Follow Player"): a helicopter of the player's flight (a wingman, the Hip of a drone
  // hunt) that flies the player's route as he plans it, from its own place at the start and home
  // to it, at its own height and speed (followPlayerRoute).
  bool follow = false;
  std::vector<MissionWaypoint> route;
};

struct MissionTask {
  int side = 0;  // 1 ours, 2 enemy
  int type = 0;
  bool user = false;  // the player's flight: its first drone is the player
  std::vector<MissionDrone> drones;
};

struct Mission {
  std::string name;   // e.g. AFGHNC01
  int zone = 0;       // 1 Kazakhstan, 2 Korea, 3 Afghanistan, 4 Saratov
  int level = 0;      // campaign level
  // Briefing.
  std::string title;
  std::vector<std::pair<std::string, std::string>> facts;  // e.g. {"Targets", "Heavy Machine Gun Sites"}
  std::vector<std::string> paragraphs;
  // The debriefing's words for each result (":Outstanding", ":Success", ":Failure"; an aborted
  // mission gets the failure's).
  std::string outstandingText, successText, failureText;
  // Settings.
  float startHour = 10.0f;  // local time of the take-off, hours
  // The autorotation drill (ours, ":Autorotation" m): the flight starts this high over the base
  // and the task is to land with nothing driving the rotor.
  int autorotation = 0;
  int weather = 0;
  float windSpeed = 0.0f, windDirection = 0.0f;
  // Kills of objects marked Success needed for success; of objects marked Success or
  // Outstanding for an outstanding result; losses of objects marked Failure that fail it.
  int successCount = 0, outstandingCount = 0, failureCount = 0;
  // The player's stores as the mission loads them (":Weapon": slot, store code). Slots 0-1 the
  // inner pylons, 2-3 the outer, 4-7 the wingtips' 9M114 tubes (8-10 the gun and the decoys).
  // Codes: 9 R-60, 10 UPK-23, 11 KMGU-2, 12 FAB-500, 13 FAB-250, 14 OFAB-250, 15 ZAB-500 (FAE),
  // 16 B-8V20 (S-8), 17 UB-32 (S-5), 18 9M114; the remake's 30 interceptor drones.
  std::vector<std::pair<int, int>> weapons;
  // Men the player is to take aboard (pick-ups, casualties) and to put out (troops) for success.
  int humansLoaded = 0, humansDeployed = 0;
  // Units that are wrecks from the start (":Destroy": the ruins of a village), m east and north.
  std::vector<glm::vec2> destroyed;
  // ":Gameplay Sides": the side (1 ours, 2 enemy, 0 none given) of each cell of the zone's
  // 64 x 64 grid of 16384 ft, [x * 64 + y].
  static constexpr int kSideCells = 64;
  std::vector<uint8_t> cellSides;
  int sideAt(glm::vec2 pos) const;  // m east and north
  // ":Gameplay AAA", "SAM", "Tanks", "APC": where the game puts units of its own (of the side of
  // their cell), m east and north.
  std::vector<glm::vec2> gameplayAaa, gameplaySam, gameplayTanks, gameplayApc;
  std::vector<MissionTask> tasks;

  const MissionDrone* player() const;
  MissionDrone* player() { return const_cast<MissionDrone*>(static_cast<const Mission*>(this)->player()); }
  std::string fact(const std::string& key) const;
};

// The followers' routes (MissionDrone::follow) after the player's route has been changed: out from
// their own starting points over the player's turning points (as many as each had, taken round
// again if it had more) and back to their own end points, timed at their own speed.
void followPlayerRoute(Mission& m);


struct FormationMember {
  int object = 0;            // 3DOBJS.BIN index
  glm::vec3 offset{0.0f};    // m: right, forward, up
};

struct Formation {
  std::string name;  // without the editor's notes ("Mujaheddin with DSHK Machine Gun")
  std::vector<FormationMember> members;
};

// A directory of the remake's own missions (the same files, named in capitals: UKRAIC01.DTA...),
// searched before the original's MISSDATA.
void addMissionDir(const std::string& dir);
// `name` such as "AFGHNC01" (case does not matter).
bool loadMission(const std::string& gameDir, const std::string& name, Mission& out, std::string& error);
// Names of the missions whose names start with `prefix` (e.g. "AFGHNC"), sorted.
std::vector<std::string> listMissions(const std::string& gameDir, const std::string& prefix);
// A mission's campaign level (its .MIS), 0 when it has none.
int missionLevel(const std::string& gameDir, const std::string& name);
// A text file of the missions' directories (ours first, then MISSDATA) by its sections
// (":Description" ... as "Description"); empty when there is none (the campaigns' AFGHNL00.TXT ...).
std::vector<std::pair<std::string, std::string>> loadTextSections(const std::string& gameDir, const std::string& name);
// Formations by their 1-based number; element 0 is empty.
bool loadFormations(const std::string& gameDir, std::vector<Formation>& out, std::string& error);

}  // namespace gamedata
