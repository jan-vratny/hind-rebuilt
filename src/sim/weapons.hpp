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

// Mi-24V armament: the Yak-B machine gun in the USPU-24 chin turret and the stores on the stub
// wings: three pairs of pylons (inner, outer, wingtip), each pair carrying the same store, as the
// original game loads them. Projectiles fly ballistically and burst on the ground. Body axes as
// in heli.hpp (x right, y up, nose to -z).
namespace sim {

// Mine: the anti-personnel mines a KMGU-2 dispenser lets go a compartment at a time. R60: the
// infra-red air-to-air missile.
// DoorGun: the GShG-7.62 of a gunner in a cabin window (Loadout::doorGun).
enum class WeaponType { YakB, Upk23, S8, S5, Fab250, Fab500, Ofab250, Zab500, Shturm, Mine, R60, DoorGun, Interceptor };
const char* weaponName(WeaponType t);
bool isBomb(WeaponType t);

enum class Pylon { Inner, Outer, Wingtip };
constexpr int kPylons = 3;
const char* pylonName(Pylon p);  // "INNER PYLONS"

// What a pair of pylons carries. The order is that of the original's payload screen.
// Interceptor: not the original's, a rack of small drones that hunt drones (the remake's
// Ukrainian war); the crew's, not the pilot's to fire (Weapons::interceptors).
enum class Store { None, B8V20, UB32, Upk23, Fab250, Fab500, Ofab250, Zab500, Kmgu, R60, Shturm, Interceptor };
constexpr int kStores = 12;

struct StoreInfo {
  const char* key;    // in hind.ini: "b8v20"
  const char* name;   // short: "B-8V20"
  const char* title;  // "B-8V20 POD: 20 S-8 80 MM ROCKETS"
  WeaponType weapon;
  int rounds;         // per store (one side)
  float mass;         // kg, loaded
  float emptyMass;    // kg, once fired (a pod stays on, a bomb is gone)
  bool fits[kPylons];
  bool available;     // flies in this version
};
const StoreInfo& storeInfo(Store s);
bool parseStore(const std::string& key, Store& out);
// Whether the store can go on that pylon; `why` gets the original game's complaint if not.
bool storeFits(Store s, Pylon p, std::string* why = nullptr);

struct Loadout {
  Store inner = Store::B8V20;
  Store outer = Store::B8V20;
  Store wingtip = Store::None;
  // A GShG-7.62 put out of the cabin's left and right window, a gunner at each.
  bool doorGun[2] = {false, false};
  Store& at(Pylon p) { return p == Pylon::Inner ? inner : p == Pylon::Outer ? outer : wingtip; }
  Store at(Pylon p) const { return p == Pylon::Inner ? inner : p == Pylon::Outer ? outer : wingtip; }
  float mass() const;  // kg of all the stores (and door guns with their gunners), loaded
  // The interceptor drones' rack hangs from the inner pylons but spans the outer ones too (under
  // the wing between them, out of the door guns' way): nothing goes on those then.
  bool outerTaken() const { return inner == Store::Interceptor; }
  void settle() {
    if (outerTaken()) outer = Store::None;
  }
};
// A door gun with its mount, its rounds and its gunner, kg: the GShG-7.62 19, kDoorGunRounds
// linked 7.62 x 54R rounds of about 25 g, a mount of some 10, the gunner with kit 85.
constexpr int kDoorGunRounds = 1500;
constexpr float kDoorGunMass = 19.0f + kDoorGunRounds * 0.025f + 10.0f + 85.0f;
// The door guns in hind.ini: "none", "left", "right" or "both".
const char* doorGunsKey(const Loadout& l);
bool parseDoorGuns(const std::string& key, Loadout& l);
// The stores of the reference mass (Helicopter::kRefMass): four B-8V20 pods.
float referenceStoresMass();

struct Projectile {
  WeaponType type;
  glm::vec3 pos, vel;
  float age = 0.0f;
  bool tracer = false;  // drawn as a glowing streak
  glm::vec3 carrier{0.0f};  // the speed of the helicopter that fired it (its tracer's streak is seen from there)
  int target = -1;      // R-60, interceptor: what it goes for (index into Targets::list)
  // An interceptor flown by a player at a door (seat 2 left, 3 right; 0: none) and how he
  // steers it, -1..1 (x to the left, y up); a manual one finds no target of its own.
  uint8_t owner = 0;
  bool manual = false;
  glm::vec2 steer{0.0f};
  glm::vec2 turning{0.0f};  // how far into its turn it is: follows `steer` (kInterceptorRoll)
  bool locked = false;      // its player locked it on `target` (Weapons::lockInterceptor)
  // The crew's: the door gunner guiding it onto its drone (1 left, 2 right; 0 once it is within
  // Weapons::kInterceptorLock of it and on its own).
  uint8_t guide = 0;
  bool law = false;  // (an S5 so far as the effects go:) a lone bowman's M72 LAW rocket
};

// A KMGU-2 compartment let go: where and how the helicopter flew at that moment.
struct MineRelease {
  glm::vec3 pos, vel;
  float height;  // m above the ground
};

// Something hit the ground or a vehicle: the renderer turns it into a dust puff or an explosion.
// A destroyed vehicle gets an impact of its own (`destroyed`) at its position.
struct Impact {
  WeaponType type;
  glm::vec3 pos;
  int target = -1;         // vehicle hit, index into Targets::list
  bool destroyed = false;  // the vehicle `target` blew up
  int8_t hind = -1;        // a Hind of the network game hit (OtherHind::id)
};

// A round (a rocket, a missile, a drone) of another Hind on this one: `at` in the world, what it
// found going by its weapon (`by` for the debriefing); and the blast of one going off near it (false:
// too far). Hits only the pilot's game counts (see main).
void roundOnHind(Helicopter& h, WeaponType w, glm::vec3 at, const std::string& by);
bool blastOnHind(Helicopter& h, WeaponType w, glm::vec3 at, const std::string& by);
// Whether the segment a-b passes within `margin` of a Hind's hull; `at` then in its body axes.
bool hullHit(const Helicopter& heli, glm::vec3 a, glm::vec3 b, float margin, glm::vec3& at);

class Weapons {
public:
  friend class net::Mirror;  // copies the state to the gunner's game
  // USPU-24 turret limits (manual, figure 5.6), radians.
  static constexpr float kTurretAzLimit = 1.0472f;   // +-60 deg
  static constexpr float kTurretElUp = 0.3491f;      // 20 deg
  static constexpr float kTurretElDown = -1.0472f;   // 60 deg
  static constexpr float kTurretRate = 1.2f;         // rad/s slew rate
  static constexpr float kTurretSlewSound = 0.035f;  // rad behind its aim: the motors heard
  static constexpr glm::vec3 kTurretPivot{0.0f, -1.30f, -5.40f};
  // Where the stores hang (body axes): the left pylon of each pair, x mirrored on the right.
  static glm::vec3 pylonPoint(Pylon p);
  // Direction to fire the Yak-B from `muzzle` (moving with `ownVel`) to meet a target at
  // `target` moving with `targetVel`, worked out on the round's own path (drag slows its fall
  // as well as its flight); `time` gets the time of flight.
  static glm::vec3 gunAim(glm::vec3 muzzle, glm::vec3 ownVel, glm::vec3 target, glm::vec3 targetVel, float* time = nullptr,
                          WeaponType gun = WeaponType::YakB);
  // The game's other Hinds, which our rounds can hit (id: 0 the pilot's, k + 1 wingman k); the
  // door gunners firing by themselves mind them.
  struct OtherHind {
    const Helicopter* heli = nullptr;
    int8_t id = -1;
  };
  std::vector<OtherHind> otherHinds;
  // The door gunners: each fires by himself at the enemy he sees nearest in his window's field
  // (a drone first), within kDoorGunReach, in bursts aimed with lead; at things in the air only
  // when the shot is good enough (doorShot).
  static constexpr float kDoorGunReach = 900.0f;
  // How good a door gunner's shot at something in the air is, 0..1, from where it is (`rel`,
  // from the gun) and how it moves against the helicopter (`relVel`): near, its speed matched,
  // and below. He opens fire from kDoorOpenFire, when it stops getting better (kDoorGaining).
  static float doorShot(glm::vec3 rel, glm::vec3 relVel);
  // Its three parts, each 0..1 (doorShot is their product).
  struct DoorShotParts {
    float near, matched, below;
  };
  static DoorShotParts doorShotParts(glm::vec3 rel, glm::vec3 relVel);
  static constexpr float kDoorOpenFire = 0.3f;
  static constexpr float kDoorGaining = 0.005f;  // per second: still getting better, worth waiting
  static constexpr glm::vec3 kDoorPoint{1.25f, 0.1f, -0.6f};  // the right window (x mirrored on the left)
  static constexpr float kDoorBarrel = 0.68f;  // from the gun's pivot to its muzzle (the flash hider's end)
  // Where the door guns turn (the right one, body axes): the post at kDoorPoint, or where the
  // game draws them in the original's Mi-24 (its doorway); the rounds leave the muzzle.
  static inline glm::vec3 doorPivot = kDoorPoint;
  int doorRounds(int side) const { return doorRounds_[side]; }
  int doorTarget(int side) const { return door_[side].target; }
  // Where a door gun points (body axes), for drawing it.
  glm::vec3 doorAim(int side) const { return doorAim_[side]; }
  // A player at a door gun (side 0 left, 1 right; the network game's, or ours at F4): it fires
  // where he aims it (body axes) while he holds the trigger, within its field; a door nobody
  // holds is the computer's as before. clearDoorGunners() each frame before they are set.
  void clearDoorGunners() { doorManned_[0] = doorManned_[1] = false; }
  void setDoorGunner(int side, glm::vec3 dirBody, bool trigger) {
    if (side < 0 || side > 1) return;
    doorManned_[side] = true, doorDir_[side] = dirBody, doorTrigger_[side] = trigger;
  }
  bool doorManned(int side) const { return side >= 0 && side < 2 && doorManned_[side]; }
  bool anyDoorManned() const { return doorManned_[0] || doorManned_[1]; }
  // Whether a direction (body axes) is in the field of the gun in that side's window: out of the
  // window, 70 degrees fore and aft of square, 70 down and 30 up.
  static bool doorField(int side, glm::vec3 dirBody);
  // R-60, as in the original: its seeker locks on the target the WSO tracks (r60Track), once
  // that is an enemy aircraft, helicopter or drone within kR60Cone of the nose and its lock
  // range; without a lock it leaves the rail unguided and goes off where it hits.
  static constexpr float kR60Cone = 0.35f, kR60Range = 7000.0f;
  // The interceptor drones on the inner pylons, the crew's: a door gunner (the computer, when
  // no player is at a door) lets one go at a jet drone within kInterceptorReach that none is
  // after yet (the slower ones he leaves to his gun), or at the pilot's order (orderInterceptor)
  // at the nearest drone; it flies at kInterceptorSpeed and steers itself onto it by
  // proportional navigation at up to kInterceptorTurn, its charge going off within
  // kInterceptorFuse; kInterceptorLife of battery. Till it is within kInterceptorLock of its
  // drone he guides it: no other one from him, and his gun is silent.
  static constexpr float kInterceptorReach = 5000.0f, kInterceptorSpeed = 111.0f, kInterceptorTurn = 60.0f,
                         kInterceptorFuse = 4.0f, kInterceptorLife = 150.0f;
  // Each hangs nose forward on its rack (a plane with a pusher prop, not a quadcopter): it goes
  // off ahead and for kInterceptorClear turns away from the helicopter at kInterceptorPeel, only
  // then steered or steering itself.
  static constexpr float kInterceptorClear = 1.2f, kInterceptorPeel = 0.2f;
  // Where they leave the rack from (the left side's; x mirrored on the right): under the wing
  // between the pylons, towards its trailing edge (render::buildStore draws it).
  static constexpr glm::vec3 kInterceptorRackPoint{-2.05f, -1.15f, 2.0f};
  // A player at a door flies them himself (his camera on the drone): he sends one out (false
  // when none is left or his own is still flying) and steers it (at up to
  // kInterceptorSteerRate, and no more than kInterceptorTurn across its flight at full speed,
  // less slower (its wings' lift), so never tighter than some 200 m round; it takes
  // kInterceptorRoll to bank into a turn and out of it), its
  // charge going off within kInterceptorManualFuse of an enemy aircraft or drone, until he lets
  // go of it: then it finds its own way. While a player is at a door (interceptorsCrewed) the
  // computer launches none. Flown into our own helicopter it goes off there: into the rotor
  // disc, or (kInterceptorSelfRotor of the time) its fragments up into the blades, it brings
  // the helicopter down; else it takes out the engine on that side, or the tail rotor.
  static constexpr float kInterceptorSteerRate = 1.3f, kInterceptorManualFuse = 6.0f, kInterceptorRoll = 0.15f,
                         kInterceptorSelfRotor = 0.2f;
  // His camera marks the enemy fliers within kInterceptorCamera; he picks one of them and, once
  // it is within kInterceptorLock, locks the drone on it (false: not a flier he can lock, too
  // far): from then on it flies itself into it (the Litavr's "pixel lock") and he has his
  // hands free, the drone still his to watch (interceptorWatched) but no longer to fly.
  static constexpr float kInterceptorCamera = 3000.0f, kInterceptorLock = 1000.0f;
  bool launchInterceptor(Helicopter& heli, int owner);
  // The pilot's order to the door gunners: one out at his target (`target`, the WSO's or the
  // Hip's) when it is an enemy drone, else at the nearest one in reach; "" when it went, else
  // why not.
  std::string orderInterceptor(Helicopter& heli, int target = -1);
  bool guiding(int side) const;  // the door gunner (0 left, 1 right) guides an interceptor
  void steerInterceptor(int owner, glm::vec2 steer);
  void releaseInterceptor(int owner);
  bool lockInterceptor(int owner, const Targets& targets, int target);
  static bool interceptorTarget(const Vehicle& v);  // an enemy flier its charge is for
  const Projectile* interceptorOf(int owner) const;       // the one he flies
  const Projectile* interceptorWatched(int owner) const;  // ... or has locked on its target
  bool interceptorsCrewed = false;
  // The crew sends them at any enemy drone, a Shahed too (an AI Hind's: it has no door gunner
  // at them); else at the jet ones only, the Shaheds left to the door guns.
  bool interceptAny = false;
  // The target the R-60 would lock on now (`tracked` if it qualifies), or -1.
  static int r60Seek(const Helicopter& heli, const Targets& targets, int tracked);
  int r60Track = -1;  // the WSO's target, set each frame
  // How far the R-60's seeker sees that unit from where the helicopter is: a jet's or a
  // helicopter's engines out to kR60Range; a Shahed's little piston engine, faint and cooled by
  // the air, from behind out to 1.5 km and from elsewhere only 600 m.
  static float r60LockRange(const Vehicle& v, glm::vec3 from);
  // Range (m) within which the weapon is worth firing.
  static float effectiveRange(WeaponType t);
  // Range (m) inside which it is no good (a 9M114 cannot steer onto the target in time).
  static float minimumRange(WeaponType t);

  explicit Weapons(const world::Terrain& terrain) : terrain_(terrain) {}

  void load(const Loadout& l);
  // Cycles navigation mode (weapons safe) and the weapons with rounds left, arming them.
  void selectNext();
  bool select(WeaponType t);  // arms this one, if it has rounds
  void makeSafe();
  // The gunner of the network game: the turret gun and the 9M114s are his alone, chosen and
  // fired by him (`armed`, `weapon`, his trigger); the pilot is put back to navigation mode if
  // he had one of them armed. The stores (rockets, bombs, gun pods, R-60s, mines) either of
  // them may choose and fire, each with his own trigger; the interceptors are the crew's.
  static bool gunnersWeapon(WeaponType t) { return t == WeaponType::YakB || t == WeaponType::Shturm; }
  // What the gunner's E cycles through: the turret gun, the 9M114s, then the stores from the
  // inner pylons out, those with rounds left.
  std::vector<WeaponType> gunnerChoices() const;
  void setGunner(bool manned, bool armed, WeaponType weapon, bool trigger);
  bool gunnerManned() const { return gunner_.manned; }
  bool gunnerArmed() const { return gunner_.armed; }
  WeaponType gunnerWeapon() const { return gunner_.weapon; }
  bool armed() const { return armed_; }
  // Safe by the safe key (Ctrl+E) rather than cycled round to navigation mode.
  bool madeSafe() const { return madeSafe_; }
  WeaponType selected() const { return selected_; }
  int rounds(WeaponType t) const;
  // Rounds left on a pair of pylons (both sides together), as the WSO's weapon panel counts them.
  int pylonRounds(Pylon p) const { return left_[int(p)][0] + left_[int(p)][1]; }
  // Rounds left on one side of a pair (side 0 left, 1 right).
  int sideRounds(Pylon p, int side) const { return left_[int(p)][side]; }
  const Loadout& loadout() const { return loadout_; }
  // kg of the stores still on the wings.
  float storesMass() const;
  // e.g. "GUN 1470", "S-8 80", "NAV"
  std::string status() const;

  // Where a bomb released now would hit (false when it would not reach the ground in time).
  bool bombImpact(const Helicopter& heli, glm::vec3& out) const;

  // 9M114 command guidance: the sight's line from `eye` along `dir` (world); the missiles in
  // flight steer onto it. Without a line (the sight lost it) they fly on straight.
  void guide(glm::vec3 eye, glm::vec3 dir) { losEye_ = eye, losDir_ = dir, guided_ = true; }
  void unguided() { guided_ = false; }
  // Seconds until the first 9M114 in flight reaches `target` (< 0: none in flight).
  float missileTimeTo(glm::vec3 target) const;

  // Turret direction relative to the airframe: az + = left, el + = up (the seat look convention).
  void aimTurret(float az, float el) { turretTargetAz_ = az, turretTargetEl_ = el; }
  float turretAz() const { return turretAz_; }
  float turretEl() const { return turretEl_; }
  // The turret is swinging round to a new direction (more than a couple of degrees behind its
  // aim), not just holding a target steady while the helicopter moves under it.
  bool turretSlewing() const { return turretSlewing_; }
  // Unit vector along the gun in body axes.
  glm::vec3 turretDirBody() const;

  // trigger: fire button held. Fills `impacts` with what hit the ground or `targets` during this step.
  void step(Helicopter& heli, bool trigger, float dt, std::vector<Impact>& impacts, Targets* targets = nullptr);

  std::vector<Projectile> shots;
  // What left the helicopter (one entry a round), for the sounds; whoever reads it clears it.
  std::vector<WeaponType> fired;
  // The mine compartments let go (whoever reads it clears it).
  std::vector<MineRelease> mineReleases;
  bool infiniteAmmo = false;
  // Quickstart Arcade: the door guns do count their rounds (the other weapons are infinite), and
  // run dry they take kDoorReload to load, then have all of them again.
  bool doorReloads = false;
  static constexpr float kDoorReload = 6.0f;
  bool doorReloading(int side) const { return doorReload_[side] > 0.0f; }
  // Quickstart over Ukraine: the R-60s fired off, the rails are loaded again kR60Reload later
  // (in the air: the stream of Banderols does not end either).
  bool r60Reloads = false;
  // Whose the kills are (an AI wingman's Hind has its own Weapons).
  Killer killer = Killer::Player;
  std::string killerName;
  static constexpr float kR60Reload = 20.0f;
  // The door gunners fire by themselves (free fire), or only when someone mans their gun (hold fire).
  bool doorGunnersFree = true;

  // As in the original: sat down anywhere for a minute during the mission, the gun gets its
  // ammunition again (and the door guns theirs, the only way they do outside Arcade), once a flight. Call every step; reloads() counts the times it happened.
  static constexpr float kReloadTime = 60.0f;
  static constexpr int kYakBRounds = 1470;
  void groundReload(const Helicopter& heli, float dt);
  int reloads() const { return reloads_; }

private:
  // One round of the selected weapon from the next pylon in turn; false when none is left.
  bool fire(Helicopter& heli, WeaponType t);
  // The trigger of one crew member on weapon `t`: rounds as the weapon fires them.
  void pull(Helicopter& heli, WeaponType t, bool armed, bool trigger, bool& was, float& cooldown, float dt);
  void killed(Helicopter& heli, const Targets& targets, int k, std::vector<Impact>& impacts);
  void doorGuns(Helicopter& heli, float dt);
  float doorReload_[2] = {0.0f, 0.0f};  // s left of loading a door gun
  void interceptors(Helicopter& heli, float dt);
  // An interceptor off the racks at `target` by the door gunner at `side` (from his side's rack
  // while it has any).
  void crewLaunch(Helicopter& heli, int target, int side);
  void spawn(WeaponType t, const Helicopter& heli, glm::vec3 muzzleBody, glm::vec3 dirBody, float speed,
             float spread, bool tracer);

  const world::Terrain& terrain_;
  const Targets* targets_ = nullptr;  // during step()
  Loadout loadout_;
  int yakB_ = 0;
  float grounded_ = 0.0f;  // s sat still on the ground
  bool reloadUsed_ = false;
  int reloads_ = 0;
  float r60Reload_ = 0.0f;  // s until the R-60s are loaded again (r60Reloads), 0 not loading
  int left_[kPylons][2] = {};  // rounds per pylon, left and right
  struct DoorGunner {
    int target = -1;
    float look = 0.0f, burst = 0.0f, pause = 0.0f, cooldown = 0.0f;
    glm::vec2 error{0.0f};  // aim error of the burst, rad
    int fired = 0;
    // His shot at the one in the air he follows: how good it was, how fast it is getting better
    // (per second, smoothed), whether he has opened fire on it.
    int watched = -1;
    float lastShot = 0.0f, gaining = 0.0f;
    bool engaged = false;
  };
  DoorGunner door_[2];
  glm::vec3 doorAim_[2] = {{-1.0f, -0.1f, 0.0f}, {1.0f, -0.1f, 0.0f}};
  bool doorManned_[2] = {false, false};
  glm::vec3 doorDir_[2] = {{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
  bool doorTrigger_[2] = {false, false};
  int doorRounds_[2] = {};
  WeaponType selected_ = WeaponType::YakB;
  bool armed_ = false, madeSafe_ = false;
  float turretAz_ = 0, turretEl_ = 0, turretTargetAz_ = 0, turretTargetEl_ = 0;
  bool turretSlewing_ = false;
  float cooldown_ = 0;  // seconds until the next round may leave
  int fired_ = 0;       // rounds fired (tracer every few, alternating sides and pods)
  float interceptWait_ = 0;  // s until the crew may let the next interceptor go
  int interceptSide_ = 0;    // the pylon it comes off last (they alternate)
  bool triggerWas_ = false;
  struct Gunner {
    bool manned = false, armed = false, trigger = false, was = false;
    WeaponType weapon = WeaponType::YakB;
    float cooldown = 0.0f;
  };
  Gunner gunner_;
  glm::vec3 losEye_{0.0f}, losDir_{0.0f, 0.0f, -1.0f};
  bool guided_ = false;
  unsigned rng_ = 12345u;
  float random();  // -1..1
};

}  // namespace sim
