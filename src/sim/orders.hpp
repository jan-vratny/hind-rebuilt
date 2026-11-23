#pragma once
#include <string>
#include <vector>

#include "sim/heli.hpp"
#include "sim/targets.hpp"
#include "sim/weapons.hpp"
#include "world/terrain.hpp"

// The player's orders to our other helicopters, as the original's (manual p. 28): to the wingman
// (the mission's AI Hind) or to the whole formation (every helicopter of ours in the air): go
// home, follow me, attack my target, resume the flight plan. Each answers on the radio.
namespace sim {

enum class Order { Home, Follow, Attack, Plan };

class Orders {
public:
  // An order (`formation`: to all of ours, else to the wingman); `target` the unit to attack
  // (the WSO's, the Hip's), -1 none. Returns the answer's radio line (gamedata::snd), -1 when
  // there is nobody to give it to, and the text for the display in `said`.
  int give(Order o, bool formation, const Helicopter& heli, Targets& targets, int target, std::string& said);
  // The ordered fly as told: in formation off the player's right, at the target (firing at it),
  // home; what their fire did goes into `impacts`.
  void step(const Helicopter& heli, Targets& targets, const world::Terrain& terrain, float dt, std::vector<Impact>& impacts);
  void reset() { units_.clear(); }

  // Our helicopter units: the wingman (the first AI Hind of ours), -1 none; all of ours in the air.
  static int wingman(const Targets& targets);
  static std::vector<int> formation(const Targets& targets);

private:
  struct Unit {
    int index = -1;
    Order order = Order::Plan;
    int slot = 0;     // its place in the formation behind the player
    int target = -1;  // attacking it
    float fire = 0.0f;  // s to its next burst
    bool routed = false;  // sent home: its way there laid out
    // Its flight plan as it was when first ordered (Plan takes it back up).
    std::vector<RoutePoint> plan;
    size_t planNext = 0;
    bool planLoop = false;
  };
  Unit& unit(int index, const Targets& targets);
  std::vector<Unit> units_;
  unsigned rng_ = 77u;
  float random();
};

}  // namespace sim
