#include "sim/mission.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "gamedata/sound.hpp"

namespace sim {

namespace {

// What the objects of the original are, by a part of their names; the first match counts.
struct Rule {
  const char* part;
  const char* name;  // shown when destroyed; null: from the object's name
  UnitClass cls;
  Armour armour;
  float health;
  int radarClass = 0;
  float radarRange = 0.0f;
};
const Rule kRules[] = {
    {"Mujaheddin", "MUJAHEDDIN", UnitClass::Soldier, Armour::Soft, 1.0f},
    {"Soldier", "SOLDIER", UnitClass::Soldier, Armour::Soft, 1.0f},
    {"Infantry", "SOLDIER", UnitClass::Soldier, Armour::Soft, 1.0f},
    {"Human", "SOLDIER", UnitClass::Soldier, Armour::Soft, 1.0f},
    {"Casualty", "CASUALTY", UnitClass::Soldier, Armour::Soft, 1.0f},
    {"DShK", "DSHK HMG", UnitClass::Vehicle, Armour::Soft, 2.0f},
    {"HMG", "HMG", UnitClass::Vehicle, Armour::Soft, 2.0f},
    {"Mortar", "MORTAR", UnitClass::Vehicle, Armour::Soft, 2.0f},
    {"Mi_24", "MI-24", UnitClass::Helicopter, Armour::Light, 14.0f},
    {"Mi_17", "MI-17", UnitClass::Helicopter, Armour::Light, 12.0f},
    {"Mi_8", "MI-8", UnitClass::Helicopter, Armour::Light, 12.0f},
    {"AH_1", "AH-1 COBRA", UnitClass::Helicopter, Armour::Light, 10.0f},
    {"AH_64", "AH-64 APACHE", UnitClass::Helicopter, Armour::Light, 12.0f},
    {"MiG_29", "MIG-29", UnitClass::Aircraft, Armour::Light, 10.0f},
    {"MiG_27", "MIG-27", UnitClass::Aircraft, Armour::Light, 10.0f},
    {"MiG", "MIG", UnitClass::Aircraft, Armour::Light, 10.0f},
    {"Su_25", "SU-25", UnitClass::Aircraft, Armour::Light, 12.0f},
    {"F16", "F-16", UnitClass::Aircraft, Armour::Light, 10.0f},
    {"F_16", "F-16", UnitClass::Aircraft, Armour::Light, 10.0f},
    {"Antonov", "AN-12", UnitClass::Aircraft, Armour::Light, 16.0f},
    {"Giraffe", "GIRAFFE RADAR", UnitClass::Vehicle, Armour::Light, 10.0f, 1, 20000.0f},
    {"SPAAG", "SPAAG", UnitClass::Vehicle, Armour::Light, 12.0f, 3, 8000.0f},
    {"Rapier", "RAPIER", UnitClass::Vehicle, Armour::Light, 12.0f, 4, 10000.0f},
    {"MBT", nullptr, UnitClass::Vehicle, Armour::Heavy, 30.0f},
    {"APC", nullptr, UnitClass::Vehicle, Armour::Light, 15.0f},
    {"Sagger", "BRDM-2 SAGGER", UnitClass::Vehicle, Armour::Light, 12.0f},
    {"Howitzer", "D-30 HOWITZER", UnitClass::Vehicle, Armour::Light, 10.0f},
    {"Jeep", "UAZ-469", UnitClass::Vehicle, Armour::Soft, 4.0f},
    {"Truck", "TRUCK", UnitClass::Vehicle, Armour::Soft, 5.0f},
    {"Bowser", "FUEL TRUCK", UnitClass::Vehicle, Armour::Soft, 4.0f},
    {"Tent", "TENT", UnitClass::Structure, Armour::Soft, 4.0f},
    {"Pallets", "SUPPLIES", UnitClass::Structure, Armour::Soft, 6.0f},
    {"Bladders", "FUEL BLADDERS", UnitClass::Structure, Armour::Soft, 3.0f},
    {"Breastwork", "BREASTWORK", UnitClass::Structure, Armour::Heavy, 60.0f},
    {"Bridge", "BRIDGE", UnitClass::Structure, Armour::Heavy, 80.0f},
    {"Ruin", "RUIN", UnitClass::Structure, Armour::Heavy, 40.0f},
    {"Mosque", "MOSQUE", UnitClass::Structure, Armour::Heavy, 40.0f},
    {"House", "HOUSE", UnitClass::Structure, Armour::Light, 20.0f},
    {"Hangar", "HANGAR", UnitClass::Structure, Armour::Heavy, 60.0f},
    {"Barracks", "BARRACKS", UnitClass::Structure, Armour::Light, 30.0f},
    {"Yurt", "YURT", UnitClass::Structure, Armour::Soft, 4.0f},
};

// What the unit shoots with, from its name in the original game.
Arms armsOf(const std::string& name) {
  static const std::pair<const char*, Arms> table[] = {
      {"SAM_7", Arms::Manpads}, {"RPG_7", Arms::Rpg},      {"AK_47", Arms::Rifle},  {"Mujaheddin_Figure", Arms::Rifle},
      {"DShK", Arms::Hmg},      {"HMG", Arms::Hmg},        {"M113", Arms::Hmg},     {"SPAAG", Arms::Aaa},
      {"Rapier", Arms::Sam},    {"Vulcan", Arms::Aaa},     {"Stinger", Arms::Manpads}, {"SA8_ROMB", Arms::Sam},
      {"AH_64", Arms::Gunship}, {"AH64", Arms::Gunship},   {"AH_1", Arms::Gunship}, {"Mi_24", Arms::Gunship},
      {"MiG", Arms::Fighter},   {"F16", Arms::Fighter},    {"F_16", Arms::Fighter}, {"Su_25", Arms::Fighter},
      {"MBT", Arms::TankGun},   {"APC", Arms::Hmg},
  };
  for (const auto& [part, arms] : table)
    if (name.find(part) != std::string::npos) return arms;
  return Arms::None;
}

// Whether a building of the zone counts as a target where it stands at a target point: not
// trees, scrub, roads, runway markings or ruins.
bool targetBuilding(const std::string& name) {
  if (name.find("Bridge") != std::string::npos) return true;
  for (const char* part : {"copse", "scrub", "Ruin", "Road", "Runway", "Track", "Floodlight", "Fence", "Wall", "Rock", "Tree"})
    if (name.find(part) != std::string::npos) return false;
  return true;
}

std::string display(const std::string& original) {
  std::string s = original;
  for (char& c : s) c = c == '_' ? ' ' : char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

VehicleKind unitKind(const gamedata::OriginalData& original, int object) {
  static std::map<int, VehicleKind> known;
  auto it = known.find(object);
  if (it != known.end()) return it->second;
  const std::string name = original.loaded() ? original.name(object) : std::string();
  VehicleKind k = name.empty() ? VehicleKind::Count : findVehicleType(name);
  if (k == VehicleKind::Count) {
    VehicleType t;
    t.original = name.empty() ? "object " + std::to_string(object) : name;
    t.name = display(name);
    for (const Rule& r : kRules) {
      if (name.find(r.part) == std::string::npos) continue;
      if (r.name) t.name = r.name;
      t.cls = r.cls;
      t.armour = r.armour;
      t.health = r.health;
      t.radarClass = r.radarClass;
      t.radarRange = r.radarRange;
      break;
    }
    t.object = name.empty() ? -1 : original.cleanest(object);
    t.arms = armsOf(name);
    gamedata::Model m = name.empty() ? gamedata::Model() : original.model(t.object);
    if (!m.polys.empty()) {
      t.lo = m.lo;
      t.hi = m.hi;
      // What stands about the zone (not what moves through the air): by its faces, not its box.
      if (t.cls == UnitClass::Structure || t.cls == UnitClass::Vehicle)
        for (const gamedata::Polygon& p : m.polys) {
          if (p.pts.size() < 3) continue;
          const glm::vec3 n = glm::cross(p.pts[1] - p.pts[0], p.pts[2] - p.pts[0]);
          if (glm::length(n) > 1e-5f) t.faces.push_back({p.pts, glm::normalize(n)});
        }
    }
    // A man is thin: his hit box is his body, not the gun he carries.
    if (t.cls == UnitClass::Soldier) t.lo = {-0.3f, 0.0f, -0.3f}, t.hi = {0.3f, 1.8f, 0.3f};
    t.lo.y = std::min(t.lo.y, 0.0f);
    k = registerVehicleType(t);
  }
  known[object] = k;
  return k;
}

std::vector<std::string> waypointTargets(const gamedata::Mission& mission, const gamedata::OriginalData& original,
                                         const std::vector<gamedata::ZoneObject>& zoneObjects) {
  std::vector<std::string> out;
  const gamedata::MissionDrone* player = mission.player();
  if (!player) return out;
  // What there is: the zone's buildings, the mission's units where they start.
  std::vector<std::pair<glm::vec2, int>> things;
  for (const gamedata::ZoneObject& o : zoneObjects) things.push_back({o.pos, o.object});
  for (const gamedata::MissionTask& t : mission.tasks)
    for (const gamedata::MissionDrone& d : t.drones)
      if (d.object > 0 && !d.route.empty() && &d != player) things.push_back({d.route.front().pos, d.object});
  for (const gamedata::MissionWaypoint& w : player->route) {
    std::string name;
    if (w.type == 4 && original.loaded()) {
      float best = 200.0f;
      for (const auto& [pos, object] : things) {
        const float d = glm::distance(pos, w.pos);
        if (d >= best) continue;
        const std::string n = gamedata::wsoTypeName(gamedata::wsoTypeLine(original.name(object), true));
        if (!n.empty()) best = d, name = n;
      }
    }
    out.push_back(name);
  }
  return out;
}

bool missionLoadout(const gamedata::Mission& mission, Loadout& out) {
  // The door guns: both where the Shaheds are hunted (the remake's missions), else none.
  bool shaheds = false;
  for (const gamedata::MissionTask& task : mission.tasks)
    for (const gamedata::MissionDrone& d : task.drones)
      shaheds = shaheds || d.name.rfind("Shahed", 0) == 0 || d.name == "Geran_3" || d.name == "Gerbera" || d.name == "Banderol";
  out.doorGun[0] = out.doorGun[1] = shaheds;
  if (mission.weapons.empty()) return shaheds;
  static const std::pair<int, Store> codes[] = {{9, Store::R60},     {10, Store::Upk23},   {11, Store::Kmgu},
                                                {12, Store::Fab500}, {13, Store::Fab250},  {14, Store::Ofab250},
                                                {15, Store::Zab500}, {16, Store::B8V20},   {17, Store::UB32},
                                                {18, Store::Shturm}, {30, Store::Interceptor}};  // 30: the remake's
  // The pylons as the mission has them.
  out.inner = out.outer = out.wingtip = Store::None;
  for (const auto& [slot, code] : mission.weapons) {
    if (slot > 7 || (slot % 2 && slot < 4)) continue;  // one of a pair says it
    const Pylon p = slot < 2 ? Pylon::Inner : slot < 4 ? Pylon::Outer : Pylon::Wingtip;
    for (const auto& [c, store] : codes)
      if (c == code && storeFits(store, p)) out.at(p) = store;
      else if (c == code && store == Store::Interceptor) out.inner = store;  // its rack hangs from the inner pylons
  }
  out.settle();
  return true;
}

void addScenery(const std::vector<gamedata::ZoneObject>& objects, const gamedata::OriginalData& original, glm::vec2 origin,
                Targets& targets) {
  if (!original.loaded()) return;
  for (const gamedata::ZoneObject& o : objects) {
    const glm::vec2 p{o.pos.x - origin.x, origin.y - o.pos.y};
    if (original.name(o.object).empty()) continue;
    Vehicle& v = targets.list[size_t(targets.add(unitKind(original, o.object), p.x, p.y, o.heading))];
    v.side = Side::Neutral;
    v.armed = false;
    v.scenery = true;
  }
}

void MissionState::start(const gamedata::Mission& mission, const std::vector<gamedata::Formation>& formations,
                         const gamedata::OriginalData& original, glm::vec2 origin, Targets& targets,
                         const std::vector<gamedata::ZoneObject>* scenery) {
  *this = MissionState();
  mission_ = mission;
  active_ = true;
  targets.clear();
  if (scenery) addScenery(*scenery, original, origin, targets);
  auto world = [&](glm::vec2 zone) { return glm::vec2(zone.x - origin.x, origin.y - zone.y); };
  auto headingOf = [](glm::vec2 from, glm::vec2 to) {
    glm::vec2 d = to - from;
    return glm::length(d) > 0.01f ? std::atan2(d.x, -d.y) : 0.0f;
  };

  for (const gamedata::MissionTask& task : mission.tasks) {
    const Side side = task.side == 1 ? Side::Ours : task.side == 2 ? Side::Enemy : Side::Neutral;
    for (size_t di = 0; di < task.drones.size(); ++di) {
      const gamedata::MissionDrone& drone = task.drones[di];
      if (drone.route.empty()) continue;
      std::vector<glm::vec2> pts;
      for (const gamedata::MissionWaypoint& w : drone.route) pts.push_back(world(w.pos));
      if (task.user && di == 0) {
        route_ = pts;
        for (const gamedata::MissionWaypoint& w : drone.route) routeTypes_.push_back(w.type), routeAlts_.push_back(w.alt);
        reconned_.assign(route_.size(), false);
        reconTotal_ = int(std::count(routeTypes_.begin(), routeTypes_.end(), 6));
        mined_.assign(route_.size(), false);
        minesTotal_ = int(std::count(routeTypes_.begin(), routeTypes_.end(), 17));
        continue;
      }
      // Heading of the leg into each point (the first point: of the leg out of it).
      std::vector<float> headings(pts.size(), 0.0f);
      for (size_t k = 0; k < pts.size(); ++k)
        headings[k] = pts.size() < 2 ? 0.0f : k == 0 ? headingOf(pts[0], pts[1]) : headingOf(pts[k - 1], pts[k]);
      const bool moves = pts.size() >= 2 && drone.route[1].time >= 0.0f;
      // Passengers: in a cabin (route from 15) or waiting at its last point (14).
      PassengerGroup* group = nullptr;
      if (drone.onboard > 0) {
        const bool out = drone.route.front().type == 15;
        if (out || drone.route.back().type == 14) {
          passengers_.push_back({drone.onboard, out, out ? pts.front() : pts.back(), {}, 0, false});
          group = &passengers_.back();
        }
      }
      const size_t firstUnit = targets.list.size();
      if (task.type == 18)
        // A bomber with laser-guided bombs: it releases over its target point (type 4).
        for (size_t k = 0; k < drone.route.size(); ++k)
          if (drone.route[k].type == 4) bombers_.push_back({pts[k], drone.route[k].time});

      std::vector<gamedata::FormationMember> members;
      // A unit by name: a type of our own (object -1), or the original's object.
      const VehicleKind named = drone.name.empty() ? VehicleKind::Count : findVehicleType(drone.name);
      if (drone.formation > 0) {
        if (size_t(drone.formation) < formations.size()) members = formations[size_t(drone.formation)].members;
      } else if (drone.object > 0) {
        members.push_back({drone.object, glm::vec3(0.0f)});
      } else if (named != VehicleKind::Count) {
        members.push_back({-1, glm::vec3(0.0f)});
      } else if (!drone.name.empty() && original.loaded() && original.find(drone.name) > 0) {
        members.push_back({original.find(drone.name), glm::vec3(0.0f)});
      }
      for (const gamedata::FormationMember& m : members) {
        auto place = [&](size_t k) {
          float h = headings[k];
          glm::vec2 right{std::cos(h), std::sin(h)}, fwd{std::sin(h), -std::cos(h)};
          return pts[k] + right * m.offset.x + fwd * m.offset.y;
        };
        glm::vec2 p = place(0);
        int i = targets.add(m.object < 0 ? named : unitKind(original, m.object), p.x, p.y, headings[0]);
        if (isDecoy(targets.list[size_t(i)].kind)) ++decoysTotal_;
        else if (vehicleType(targets.list[size_t(i)].kind).cls == UnitClass::Drone) ++dronesTotal_;
        Vehicle& v = targets.list[size_t(i)];
        v.side = side;
        v.armed = !drone.unarmed;
        v.objective = drone.kill == gamedata::Kill::Success       ? Objective::Success
                      : drone.kill == gamedata::Kill::Outstanding ? Objective::Outstanding
                      : drone.kill == gamedata::Kill::Failure     ? Objective::Failure
                                                                  : Objective::None;
        if (moves) {
          std::vector<RoutePoint> r;
          for (size_t k = 0; k < pts.size(); ++k)
            r.push_back({place(k), drone.route[k].alt, drone.route[k].speed, drone.route[k].wait});
          // Task type 21: ground vehicles (and soldiers) going round and round.
          targets.setRoute(i, std::move(r), task.type == 21, drone.route[0].wait, size_t(std::max(drone.startWaypoint, 0)));
        }
        if (group) group->units.push_back(i);
        switch (v.objective) {
          case Objective::Success: ++successTotal_; break;
          case Objective::Outstanding: ++outstandingTotal_; break;
          case Objective::Failure: ++failureTotal_; break;
          case Objective::None: break;
        }
      }
      if (drone.cargo > 1 && firstUnit < targets.list.size()) {
        if (carriers_.size() <= size_t(drone.cargo)) carriers_.resize(size_t(drone.cargo) + 1, -1);
        carriers_[size_t(drone.cargo)] = int(firstUnit);
      }
    }
  }
  // The air defences the game puts at the mission's ":Gameplay AAA" and "SAM" points, of the side
  // of their cell: guns and launchers as the zone's missions have them (which ones the original
  // puts there is not known).
  struct Defence {
    const char* object;
    int formation;  // instead of the object, 1-based
  };
  const bool afghan = mission.zone == 3, korea = mission.zone == 2;
  const Defence aaa[2] = {{"ZSU23_Shilka_AAA", 0}, afghan ? Defence{nullptr, 134} : korea ? Defence{"M163_Vulcan", 0} : Defence{"ZSU23_Shilka_AAA", 0}};
  const Defence sam[2] = {{"SA13_Strela_Mobile_SAM", 0},
                          {afghan ? "Mujaheddin_SAM_7" : korea ? "US_Soldier_Stinger" : "Russian_Soldier_SAM_7", 0}};
  for (int kind = 0; kind < 2; ++kind)
    for (glm::vec2 z : kind == 0 ? mission.gameplayAaa : mission.gameplaySam) {
      const int cellSide = mission.sideAt(z);
      if (cellSide != 1 && cellSide != 2) continue;
      const Defence& d = (kind == 0 ? aaa : sam)[cellSide - 1];
      std::vector<gamedata::FormationMember> members;
      if (d.formation > 0 && size_t(d.formation) < formations.size()) members = formations[size_t(d.formation)].members;
      else if (d.object && original.loaded() && original.find(d.object) >= 0) members.push_back({original.find(d.object), glm::vec3(0.0f)});
      const glm::vec2 p = world(z);
      for (const gamedata::FormationMember& m : members) {
        Vehicle& v = targets.list[size_t(targets.add(unitKind(original, m.object), p.x + m.offset.x, p.y - m.offset.y, 0.0f))];
        v.side = cellSide == 1 ? Side::Ours : Side::Enemy;
      }
    }

  // Buildings in ruins from the start (no kill of anyone's).
  for (glm::vec2 z : mission.destroyed) {
    const glm::vec2 p = world(z);
    int best = -1;
    float bestD = 8.0f;
    for (size_t i = 0; i < targets.list.size(); ++i) {
      const float d = glm::distance(glm::vec2(targets.list[i].pos.x, targets.list[i].pos.z), p);
      if (targets.list[i].scenery && !targets.list[i].destroyed && d < bestD) best = int(i), bestD = d;
    }
    if (best < 0) continue;
    Vehicle& v = targets.list[size_t(best)];
    switch (v.objective) {
      case Objective::Success: --successTotal_; break;
      case Objective::Outstanding: --outstandingTotal_; break;
      case Objective::Failure: --failureTotal_; break;
      case Objective::None: break;
    }
    v.objective = Objective::None;
    v.destroyed = true;
    v.health = 0.0f;
    v.sinceDestroyed = 1e4f;  // long burnt out
  }

  // The zone's buildings at the player's target points (not on our own ground) are targets too:
  // the radar site of an attack on it, the houses of a village to be razed. Where the mission
  // gives no primary targets and needs no kills for success (its targets are such buildings),
  // they are the primary ones and half of them must go; otherwise they count with the others.
  // (Our reading of the data; how the original counts them is not known.)
  std::vector<int> atTargets;
  for (size_t k = 0; k < route_.size(); ++k) {
    if (routeTypes_[k] != 4) continue;
    for (size_t i = 0; i < targets.list.size(); ++i) {
      const Vehicle& v = targets.list[i];
      if (!v.scenery || v.destroyed || v.objective != Objective::None ||
          glm::distance(glm::vec2(v.pos.x, v.pos.z), route_[k]) > kTargetArea || !targetBuilding(vehicleType(v.kind).original) ||
          mission.sideAt(glm::vec2(v.pos.x + origin.x, origin.y - v.pos.z)) == 1)
        continue;
      atTargets.push_back(int(i));
      targets.list[i].objective = Objective::Outstanding;  // (marks it taken)
    }
  }
  const bool buildingsArePrimary = mission.successCount == 0 && successTotal_ == 0 && mission.humansDeployed == 0 &&
                                   mission.humansLoaded == 0 && bombers_.empty() && !atTargets.empty();
  for (int i : atTargets) {
    if (buildingsArePrimary) targets.list[size_t(i)].objective = Objective::Success, ++successTotal_;
    else ++outstandingTotal_;
  }
  neededPrimary_ = buildingsArePrimary ? (successTotal_ + 1) / 2 : std::min(mission.successCount, successTotal_);
  neededTotal_ = buildingsArePrimary ? neededPrimary_ : std::min(mission.successCount, successTotal_ + outstandingTotal_);
  waypoint_ = route_.size() > 1 ? 1 : 0;
}

void MissionState::nextWaypoint(Helicopter& heli, int step) {
  if (route_.size() < 2) return;
  waypoint_ = (waypoint_ + step + int(route_.size())) % int(route_.size());
  heli.setMessage(waypoint_ == 0 ? std::string("STEERING HOME") : std::string("WAYPOINT ") + letter(waypoint_), 2.0f);
}

void MissionState::step(Helicopter& heli, const Targets& targets, float dt) {
  if (!active_ || finished_) return;
  flown_ += dt;
  int success = 0, outstanding = 0, losses = 0, struck = 0, decoys = 0;
  for (const Vehicle& v : targets.list) {
    if (!v.destroyed) continue;
    if (isDecoy(v.kind)) {
      decoys += !v.spent;
      continue;
    }
    if (v.struck) {
      // A drone that got through to its target: not a kill, a loss.
      ++struck;
      continue;
    }
    if (v.objective == Objective::Success) ++success;
    else if (v.objective == Objective::Outstanding) ++outstanding;
    else if (v.objective == Objective::Failure) ++losses;
  }
  if (struck > struck_) heli.setMessage("DRONE HIT ITS TARGET", 4.0f);
  else if (losses > failureLosses_) heli.setMessage("FRIENDLY UNIT LOST", 3.0f);
  struck_ = struck;
  decoysDowned_ = decoys;
  losses += struck;
  successKills_ = success;
  outstandingKills_ = outstanding;
  failureLosses_ = losses;
  if (!announced_ && neededPrimary_ > 0 && successKills_ >= neededPrimary_ && successKills_ + outstandingKills_ >= neededTotal_) {
    announced_ = true;
    heli.setMessage("PRIMARY TARGETS DESTROYED", 4.0f);
  }

  // Remote bombing: the bomber asks for the laser on its way in, says when it sees the target lit
  // and when it lets the bombs go; they hit if the laser still holds the target when they arrive.
  for (Bomber& b : bombers_) {
    const bool onTarget = lasing_ && glm::distance(glm::vec2(spot_.x, spot_.z), b.point) < kLaserReach;
    if (b.stage == 0 && flown_ > b.release - 40.0f) {
      radio_.push_back(gamedata::snd::DroneFirst + 8);  // "Hind leader, I'm approaching the target"
      radio_.push_back(gamedata::snd::DroneFirst + 9);  // "Hind leader, laser designate"
      heli.setMessage("LASER DESIGNATE", 4.0f);
      b.stage = 1;
    }
    if (b.stage == 1 && onTarget) {
      radio_.push_back(gamedata::snd::DroneFirst + 10);  // "Comrade, target illuminated"
      b.stage = 2;
    }
    if (b.stage == 2 && flown_ > b.release) {
      radio_.push_back(gamedata::snd::DroneFirst + 11);  // "Comrade, bombs away"
      heli.setMessage("BOMBS AWAY", 3.0f);
      b.stage = 3;
      b.impact = flown_ + kBombFall;
    }
    if (b.stage == 3 && flown_ > b.impact) {
      // Guided to the spot the laser holds, or falling wide.
      b.stage = 4;
      if (onTarget) {
        bombs_.push_back(spot_);
        ++laserDone_;
        radio_.push_back(gamedata::snd::DroneFirst + 12);  // "Comrade, mission accomplished"
        heli.setMessage("TARGET DESTROYED, RETURN TO BASE", 4.0f);
      } else {
        bombs_.push_back(glm::vec3(b.point.x + 180.0f, spot_.y, b.point.y - 120.0f));
        heli.setMessage("THE BOMBS MISSED", 4.0f);
      }
    }
  }

  // Reconnaissance being sent: good if the waypoint stays within sight to the end.
  if (reconLeft_ > 0.0f) {
    const glm::vec2 at{heli.pos.x, heli.pos.z};
    if (reconPoint_ < 0 || glm::distance(at, route_[size_t(reconPoint_)]) > kReconRange || heli.crashed) {
      reconLeft_ = 0.0f;
      reconReport_ = -1;
    } else if ((reconLeft_ -= dt) <= 0.0f) {
      reconned_[size_t(reconPoint_)] = true;
      ++reconDone_;
      reconReport_ = 1;
    }
    if (reconReport_) heli.setMessage(reconReport_ > 0 ? "GOOD RECON" : "RECON BAD", 3.0f);
  }

  // Passing a waypoint steers to the next one; after the last, home.
  if (waypoint_ > 0 && glm::distance(glm::vec2(heli.pos.x, heli.pos.z), steerPoint()) < 800.0f) {
    waypoint_ = waypoint_ + 1 < int(route_.size()) ? waypoint_ + 1 : 0;
    // The last waypoint is usually home itself.
    if (waypoint_ + 1 == int(route_.size()) && glm::distance(route_.back(), route_.front()) < 1500.0f) waypoint_ = 0;
    heli.setMessage(waypoint_ == 0 ? std::string("STEERING HOME") : std::string("WAYPOINT ") + letter(waypoint_), 2.0f);
  }

  if (heli.crashed) {
    // A moment to see it happen.
    crashed_ = true;
    crashedFor_ += dt;
    finished_ = crashedFor_ > 3.0f && !othersAloft_;
    return;
  }
  if (mission_.autorotation > 0) {
    // The drill ends where he stops, wherever that is.
    if (!heli.rotorFree) freeFrom_ = -1.0f;
    else if (freeFrom_ < 0.0f) freeFrom_ = heli.radarAltitude();
    if (heli.weightOnWheels > 0.0f && drillLanding_ == 0) {
      const bool base = !route_.empty() && glm::distance(glm::vec2(heli.pos.x, heli.pos.z), route_.front()) < kDrillBase;
      drillLanding_ = freeFrom_ < kDrillCut ? 1 : base ? 3 : 2;
    }
    if (drillLanding_ != 0 && glm::length(heli.vel) < 1.0f) finished_ = true;
    return;
  }
  // Landed back home after flying a while.
  const bool home = atHome(heli.pos);
  if (flown_ > 60.0f && home && heli.weightOnWheels > 0.0f && glm::length(heli.vel) < 1.0f && flownOut_) finished_ = true;
  if (heli.radarAltitude() > 30.0f) flownOut_ = true;
}

void MissionState::laser(bool lasing, glm::vec3 spot) {
  lasing_ = lasing;
  spot_ = spot;
}

std::vector<int> MissionState::takeRadio() { return std::exchange(radio_, {}); }
std::vector<glm::vec3> MissionState::takeBombs() { return std::exchange(bombs_, {}); }

void MissionState::startRecon(Helicopter& heli) {
  if (!active_ || finished_ || reconLeft_ > 0.0f) return;
  // The nearest reconnaissance waypoint not yet reported, if it is in sight.
  reconPoint_ = -1;
  float best = kReconRange;
  for (size_t i = 0; i < route_.size() && i < routeTypes_.size(); ++i) {
    const float d = glm::distance(glm::vec2(heli.pos.x, heli.pos.z), route_[i]);
    if (routeTypes_[i] == 6 && !reconned_[i] && d < best) best = d, reconPoint_ = int(i);
  }
  reconLeft_ = kReconTime;
  heli.setMessage("TRANSMITTING RECON", kReconTime);
}

int MissionState::mineRelease(const MineRelease& r) {
  const float kmh = glm::length(glm::vec2(r.vel.x, r.vel.z)) * 3.6f;
  for (size_t i = 0; i < route_.size() && i < routeTypes_.size(); ++i) {
    if (routeTypes_[i] != 17 || mined_[i] || glm::distance(glm::vec2(r.pos.x, r.pos.z), route_[i]) > kMineReach) continue;
    if (r.height < 30.0f || r.height > 200.0f || kmh < 150.0f) return -1;
    mined_[i] = true;
    ++minesDone_;
    return 1;
  }
  return -1;
}

int MissionState::reconReport() {
  const int r = reconReport_;
  reconReport_ = 0;
  return r;
}

MissionResult MissionState::result() const {
  if (aborted_) return MissionResult::Aborted;
  if (crashed_) return MissionResult::Failed;
  if (mission_.autorotation > 0)
    return !finished_ ? MissionResult::Flying : drillLanding_ == 3 ? MissionResult::Outstanding : drillLanding_ == 2 ? MissionResult::Success : MissionResult::Failed;
  if (reconDone_ < reconTotal_ || minesDone_ < minesTotal_ || laserDone_ < int(bombers_.size()) ||
      (troopsNeeded_ && !troopsDone_))
    return finished_ ? MissionResult::Failed : MissionResult::Flying;
  if ((failureTotal_ > 0 || dronesTotal_ > 0) && mission_.failureCount > 0 && failureLosses_ >= mission_.failureCount)
    return MissionResult::Failed;
  if (successKills_ < neededPrimary_ || successKills_ + outstandingKills_ < neededTotal_)
    return finished_ ? MissionResult::Failed : MissionResult::Flying;
  if (!finished_) return MissionResult::Flying;
  if (mission_.outstandingCount > 0 && successKills_ + outstandingKills_ >= mission_.outstandingCount)
    return MissionResult::Outstanding;
  return MissionResult::Success;
}

const char* MissionState::resultName(MissionResult r) {
  switch (r) {
    case MissionResult::Outstanding: return "OUTSTANDING";
    case MissionResult::Success: return "SUCCESS";
    case MissionResult::Failed: return "FAILURE";
    case MissionResult::Aborted: return "ABORTED";
    case MissionResult::Flying: break;
  }
  return "IN PROGRESS";
}

std::vector<std::string> MissionState::debriefing(const Targets& targets, const std::string& lost) const {
  std::vector<std::string> out;
  const MissionResult r = result();
  const std::string& words = r == MissionResult::Outstanding ? mission_.outstandingText
                             : r == MissionResult::Success   ? mission_.successText
                                                             : mission_.failureText;
  if (!words.empty()) out.push_back(words), out.push_back("");
  // How it was flown, then each task: done or not (`bad` once anything has gone wrong, for the
  // waypoint targets' "However" or "also").
  bool bad = crashed_ || aborted_;
  if (crashed_) {
    out.push_back("Mission Failed - Hind destroyed.");
    if (!lost.empty()) out.push_back(lost);
  } else if (!aborted_) {
    out.push_back("Good flying.");
  }
  auto task = [&](bool ok, const char* yes, const char* no) {
    out.push_back(ok ? yes : no);
    bad = bad || !ok;
  };
  if (mission_.autorotation > 0 && !crashed_ && !aborted_)
    task(drillLanding_ >= 2, "Landed with nothing driving the rotor.", "However, the engines were not left off down to the ground.");
  if (troopsNeeded_)
    task(troopsDone_, troopsOut_ ? "Enough personnel evacuated for success." : "Enough personel deployed for success.",
         troopsOut_ ? "Not enough personnel evacuated for success." : "Not enough personel deployed for success.");
  if (minesTotal_ > 0) task(minesDone_ >= minesTotal_, "Enough mines successfully layed.", "Not enough mines successfully layed.");
  if (reconTotal_ > 0)
    task(reconDone_ >= reconTotal_, "Reconnaissance data was sent successfully.",
         reconDone_ == 0 ? "You failed to send any reconnaissance data." : "You failed to send all required reconnaissance data.");
  if (!bombers_.empty()) task(laserDone_ >= int(bombers_.size()), "The bombers' targets were designated.", "Targets not designated for the bombers.");
  if (mission_.failureCount > 0 && failureLosses_ >= mission_.failureCount) {
    out.push_back(dronesTotal_ > 0 ? "Too many drones reached their targets." : "Allied or civilian casualties too high.");
    bad = true;
  }
  // The kills the mission counts (its Success Count; a mission without one is judged by its
  // waypoint targets alone).
  const bool enough = successKills_ >= neededPrimary_ && successKills_ + outstandingKills_ >= neededTotal_;
  if (mission_.successCount > 0) {
    if (!enough) task(false, "", "Insufficient targets destroyed.");
    else if (mission_.outstandingCount > 0 && successKills_ + outstandingKills_ >= mission_.outstandingCount)
      out.push_back("Outstanding number of targets destroyed.");
    else out.push_back("Enough targets for success.");
  }
  if (successTotal_ > 0)
    out.push_back(successKills_ >= successTotal_ ? "All waypoint targets were destroyed."
                  : bad                          ? "You also failed to destroy all waypoint targets."
                                                 : "However, not all waypoint targets were destroyed.");

  // Ours: the counts against what is needed.
  out.push_back("");
  auto count = [](const char* what, int done, int of) { return std::string(what) + ": " + std::to_string(done) + " of " + std::to_string(of); };
  if (!bombers_.empty()) out.push_back(count("Laser designation, targets destroyed", laserDone_, int(bombers_.size())));
  if (minesTotal_ > 0) out.push_back(count("Minefields laid", minesDone_, minesTotal_));
  if (reconTotal_ > 0) out.push_back(count("Reconnaissance waypoints reported", reconDone_, reconTotal_));
  out.push_back(count("Primary targets destroyed", successKills_, successTotal_) + " (" + std::to_string(neededPrimary_) + " needed)");
  if (outstandingTotal_ > 0) out.push_back(count("Other targets destroyed", outstandingKills_, outstandingTotal_));
  if (neededTotal_ > neededPrimary_)
    out.push_back("All targets destroyed: " + std::to_string(successKills_ + outstandingKills_) + " (" + std::to_string(neededTotal_) + " needed)");
  if (dronesTotal_ > 0)
    out.push_back(count("Drones that reached their targets", struck_, dronesTotal_) + " (" + std::to_string(mission_.failureCount) +
                  " fail the mission)");
  if (decoysTotal_ > 0) out.push_back(count("Gerbera decoys shot down (no score)", decoysDowned_, decoysTotal_));
  if (wingmanLost_) out.push_back("The wingman's Hind was lost" + (wingmanLostBy_.empty() ? std::string(".") : " (shot down by " + wingmanLostBy_ + ")."));

  // What was destroyed by whom, as the original's lists: by the player, by each of our other
  // Hinds, by the other allied forces, by the enemy's; under each, by what it was to them.
  struct Tally {
    std::string title;
    std::map<std::string, std::map<std::string, int>> groups;  // group heading -> unit name -> count
  };
  std::vector<Tally> tallies;
  auto tallyOf = [&](const std::string& title) -> Tally& {
    for (Tally& t : tallies)
      if (t.title == title) return t;
    return tallies.emplace_back(Tally{title, {}});
  };
  for (Killer k : {Killer::Player, Killer::Wingman, Killer::Allied, Killer::Enemy}) {
    for (const Vehicle& v : targets.list) {
      if (!v.destroyed || v.killer != k) continue;
      const VehicleType& t = vehicleType(v.kind);
      if (v.scenery && v.objective == Objective::None && !targetBuilding(t.original)) continue;
      const bool theirs = v.objective == Objective::Success || v.objective == Objective::Outstanding || v.side == Side::Enemy;
      const bool ours = !theirs && (v.side == Side::Ours || v.objective == Objective::Failure);
      const char* group = nullptr;
      if (k == Killer::Enemy) group = ours ? "  Allied Targets:" : theirs ? "  Enemy Friendly Fire Casualties:" : "  Civilian Casualties:";
      else if (k == Killer::Allied) group = theirs ? "  Enemy Targets:" : ours ? "  Allied Friendly Fire Casualties:" : "  Civilian Casualties:";
      else group = theirs ? "  Enemy Targets:" : ours ? "  Friendly Fire Casualties Caused:" : "  Civilian Casualties:";
      const std::string title = k == Killer::Player    ? "Targets destroyed by you:"
                                : k == Killer::Wingman ? "Targets destroyed by " + v.killerName + ":"
                                : k == Killer::Allied  ? "Targets destroyed by other allied forces:"
                                                       : "Targets destroyed by enemy forces:";
      ++tallyOf(title).groups[group][t.name];
    }
  }
  for (const Tally& t : tallies) {
    out.push_back("");
    out.push_back(t.title);
    // The original's order of the groups: the other side's first.
    for (const char* g : {"  Enemy Targets:", "  Allied Targets:", "  Friendly Fire Casualties Caused:", "  Allied Friendly Fire Casualties:",
                          "  Enemy Friendly Fire Casualties:", "  Civilian Casualties:"}) {
      const auto it = t.groups.find(g);
      if (it == t.groups.end()) continue;
      out.push_back(g);
      // The most of a kind first.
      std::vector<std::pair<int, std::string>> kinds;
      for (const auto& [name, n] : it->second) kinds.push_back({-n, name});
      std::sort(kinds.begin(), kinds.end());
      for (const auto& [n, name] : kinds) out.push_back("  " + std::to_string(-n) + " x " + name);
    }
  }
  return out;
}

}  // namespace sim
