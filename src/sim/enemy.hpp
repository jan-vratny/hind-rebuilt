#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "world/terrain.hpp"

namespace net {
class Mirror;
}

// The enemy shooting at the helicopter. An armed unit that sees it (over the terrain) and has it
// in reach takes a few seconds to react, then fires: rifles and machine guns in bursts aimed
// ahead of it with a gunner's error that grows with its speed across the sight, RPG-7 grenades
// that burst at the end of their flight, Strela-2 missiles homing on the engines' heat,
// command-guided Rapiers. Flares lure the heat seekers away, the L-166 jammer confuses them;
// chaff spoils the radars' aim and breaks the Rapier's guidance. The two ASO-2V launchers fire
// them by themselves at irregular intervals while switched on (keys C and F), until they run out.
namespace sim {

enum class EnemyRound { Bullet, Shell, Rpg, Strela, Rapier };

struct EnemyShot {
  EnemyRound kind;
  glm::vec3 pos, vel;
  float age = 0.0f, life = 5.0f;
  float damage = 0.0f;  // airframe points of a hit
  float drag = 0.0f;
  bool tracer = false;
  // Missiles: what the seeker follows (-1 the helicopter, else a flare's id), or nothing.
  int lock = -1;
  bool lost = false, jamTried = false;
  glm::vec3 miss{0.0f};  // how far off the exhausts the seeker centres
  VehicleKind from = VehicleKind::Count;  // who fired it
  uint8_t at = 0;  // the helicopter it was fired at: 0 the player's, 1 the other (the network game's wingman)
};

// Which hour of the clock `at` is at from the helicopter ("12" ahead).
const char* clockOf(const Helicopter& heli, glm::vec3 at);

struct Flare {
  glm::vec3 pos, vel;
  float age = 0.0f;
  int id = 0;
};

// A cloud of chaff strips, drifting down.
struct Chaff {
  glm::vec3 pos, vel;
  float age = 0.0f;
};

// What happened, for the effects and the crew.
struct EnemyEvent {
  enum Type { Muzzle, Launch, Burst, Hit } type;
  glm::vec3 pos;
  EnemyRound kind;
  bool atHeli = true;  // false: fire at our units on the battlefield (sim::GroundBattle)
};

class EnemyFire {
public:
  friend class net::Mirror;  // copies the state to the gunner's game
  explicit EnemyFire(const world::Terrain& terrain) : terrain_(terrain) {}

  bool enabled = true;
  float skill = 1.0f;   // 0.5 poor shots and slow, 1.5 sharp
  bool jammer = true;   // L-166 IR jammer on
  static constexpr int kFlares = 96, kChaff = 96;  // ASO-2V cartridges: 192 in all
  int flares = kFlares, chaff = kChaff;
  // The launchers switched on: they fire by themselves until switched off or empty.
  bool autoFlares = false, autoChaff = false;
  // The network game's wingmen's Hinds: their own dispensers and jammers (their games set the
  // switches, the cartridges are counted here), one each, as step's `others`.
  struct Launchers {
    int flares = kFlares, chaff = kChaff;
    bool autoFlares = false, autoChaff = false, jammer = true;
    float flareTimer = 0.0f, chaffTimer = 0.0f;
  };
  std::vector<Launchers> wingmen;

  void reset();
  void seed(unsigned s) { rng_ = s; }
  // Flares (`n`, from the dispensers on the tail boom, left and right in turn).
  void releaseFlares(const Helicopter& heli, int n = 2);
  void releaseChaff(const Helicopter& heli, int n = 2);
  // `others`: more helicopters they fight too (the network game's wingmen; null: a place not
  // flown now): each crew shoots at the nearest, the rounds hit any. A shot's `at` is 0 for
  // `heli`, k + 1 for others[k].
  void step(Helicopter& heli, const Targets& targets, float dt, std::vector<EnemyEvent>& events,
            const std::vector<Helicopter*>& others = {});

  const std::vector<EnemyShot>& shots() const { return shots_; }
  const std::vector<Flare>& flaresInAir() const { return flareList_; }
  const std::vector<Chaff>& chaffInAir() const { return chaffList_; }

private:
  struct Crew {
    float alert = 0.0f;     // s it has watched the helicopter
    float reaction = 3.0f;  // s it needs before it shoots
    float next = 0.0f;      // s to the next look (line of sight is checked twice a second)
    bool sees = false;
    float burst = 0.0f, pause = 0.0f, cooldown = 0.0f;
    glm::vec2 error{0.0f};  // aim error of this burst, rad
    float across = 0.0f;    // m/s the target crosses the sight
    int ammo = -1;          // rockets or missiles left, -1 unlimited
    float missileWait = 0.0f;  // an aircraft's: s to its next missile
    bool init = false;
  };
  float random();   // 0..1
  float gauss();    // about N(0, 1)
  bool lineOfSight(glm::vec3 from, glm::vec3 to) const;
  void fire(Crew& c, const Vehicle& v, const VehicleType& t, Helicopter& heli, float dt, std::vector<EnemyEvent>& events);
  // An enemy aircraft's (sim::AirCombat brings it in): its cannon on the nose (a gunship's
  // turret), its missiles.
  void fireFromAir(Crew& c, const Vehicle& v, const VehicleType& t, Helicopter& heli, float dt, std::vector<EnemyEvent>& events);
  bool hitsHelicopter(const Helicopter& heli, glm::vec3 a, glm::vec3 b, float margin, glm::vec3& at) const;
  void damage(Helicopter& heli, const EnemyShot& s, glm::vec3 at);

  const world::Terrain& terrain_;
  std::vector<Crew> crews_;
  std::vector<EnemyShot> shots_;
  std::vector<Flare> flareList_;
  std::vector<Chaff> chaffList_;
  int nextFlare_ = 1;
  // From `heli` (the one the missiles fired `at` it watch), out of `stock`.
  void dropFlares(const Helicopter& heli, int count, int& stock, uint8_t at);
  void dropChaff(const Helicopter& heli, int count, int& stock, uint8_t at);
  float flareTimer_ = 0.0f, chaffTimer_ = 0.0f;
  float sinceChaff_ = 1e9f;  // s since chaff was last fired
  unsigned rng_ = 2024u;
};

}  // namespace sim
