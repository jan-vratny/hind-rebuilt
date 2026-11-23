#include "sim/troops.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "gamedata/sound.hpp"

namespace sim {

namespace snd = gamedata::snd;

namespace {

constexpr float kWalk = 1.0f, kCarry = 0.7f;  // m/s: they drag themselves along
constexpr float kHopTime = 0.6f;              // s from the cabin floor to the ground or back
constexpr float kFloor = -0.95f;              // cabin floor below the centre of gravity, m
constexpr float kDoorZ = -1.29f;              // doors ahead of the rotor axis, m

float headingTo(glm::vec3 from, glm::vec3 to) { return std::atan2(to.x - from.x, -(to.z - from.z)); }

}  // namespace

void Troops::reset() { *this = Troops(); }

void Troops::start(std::vector<PassengerGroup> groups, const std::vector<int>& carriers, int deployNeeded, int loadNeeded,
                   glm::vec2 home, Targets& targets) {
  reset();
  groups_ = std::move(groups);
  carriers_ = carriers;
  home_ = home;
  int out = 0, in = 0;
  for (const PassengerGroup& g : groups_) {
    if (g.out)
      for (int u : g.units) targets.list[size_t(u)].aboard = true;
    if (g.carrier != 1) continue;
    (g.out ? out : in) += int(g.units.size()) + (g.out ? g.men : 0);
  }
  troops_ = out;
  deployNeeded_ = std::min(deployNeeded, out);
  loadNeeded_ = std::min(loadNeeded, in);
}

std::vector<int> Troops::takeRadio() { return std::exchange(radio_, {}); }

std::vector<glm::vec2> Troops::zonesLeft() const {
  std::vector<glm::vec2> out;
  for (const PassengerGroup& g : groups_)
    if (g.carrier == 1 && !g.done) out.push_back(g.point);
  return out;
}

bool Troops::landed(const Helicopter& heli) const {
  return heli.weightOnWheels > 0.0f && glm::length(heli.vel) < 1.0f && !heli.crashed;
}

// A point by the door on one side (0 left, 1 right), `out` metres out from the hull, at the
// height of the cabin floor.
glm::vec3 Troops::door(const Helicopter& heli, int side, float out) const {
  return heli.pos + heli.orient * glm::vec3((side ? 1.0f : -1.0f) * (0.8f + out), kFloor, kDoorZ);
}

void Troops::toggleDoors(const Helicopter& heli) {
  if (heli.crashed) return;
  doors_ = !doors_;
  radio_.push_back(doors_ ? snd::CargoOpen : snd::CargoClose);
  radio_.push_back(doors_ ? snd::CargoDoorsOpen : snd::CargoDoorsClosed);
  wrongSaid_ = false;
}

// Those waiting come over, the casualties carried by the two men nearest each.
void Troops::call(PassengerGroup& g, const Helicopter& heli, Targets& targets) {
  g.done = true;
  coming_ = true;
  std::vector<int> men, stretchers;
  for (int i : g.units) {
    const Vehicle& v = targets.list[size_t(i)];
    if (v.destroyed || v.aboard) continue;
    (vehicleType(v.kind).original.find("Casualty") != std::string::npos ? stretchers : men).push_back(i);
  }
  for (int s : stretchers) {
    std::sort(men.begin(), men.end(), [&](int a, int b) {
      return glm::distance(targets.list[size_t(a)].pos, targets.list[size_t(s)].pos) <
             glm::distance(targets.list[size_t(b)].pos, targets.list[size_t(s)].pos);
    });
    int carriers = 0;
    for (; carriers < 2 && !men.empty(); ++carriers) {
      targets.list[size_t(men.front())].aboard = true;
      men.erase(men.begin());
    }
    addIn(heli, targets.list[size_t(s)].pos, Walker::Kind::Stretcher);
    walkers_.back().men = 1 + carriers;
    targets.list[size_t(s)].aboard = true;
  }
  for (int m : men) {
    addIn(heli, targets.list[size_t(m)].pos, Walker::Kind::Soldier);
    targets.list[size_t(m)].aboard = true;
  }
}

// The other helicopters' passengers get out or in when it sets down by their place.
void Troops::stepCarriers(Targets& targets, const world::Terrain& terrain) {
  for (PassengerGroup& g : groups_) {
    if (g.carrier == 1 || g.done || g.carrier >= int(carriers_.size()) || carriers_[size_t(g.carrier)] < 0) continue;
    const Vehicle& c = targets.list[size_t(carriers_[size_t(g.carrier)])];
    if (c.destroyed || glm::distance(glm::vec2(c.pos.x, c.pos.z), g.point) > kZoneRadius ||
        c.pos.y - terrain.groundHeight(c.pos.x, c.pos.z) > 15.0f)
      continue;
    g.done = true;
    int k = 0;
    for (int u : g.units) {
      Vehicle& v = targets.list[size_t(u)];
      if (v.destroyed) continue;
      if (g.out) {
        // Out beside it, then off along their route.
        const float a = c.heading + (k % 2 ? 1.57f : -1.57f);
        v.pos = c.pos + glm::vec3(std::sin(a), 0.0f, -std::cos(a)) * (4.0f + 1.5f * float(k / 2));
        v.pos.y = terrain.groundHeight(v.pos.x, v.pos.z);
        v.aboard = false;
        ++k;
      } else {
        v.aboard = true;
      }
    }
  }
}

void Troops::step(const Helicopter& heli, Targets& targets, const world::Terrain& terrain, float dt) {
  const glm::vec2 at{heli.pos.x, heli.pos.z};
  stepCarriers(targets, terrain);

  if (doors_ && landed(heli)) {
    PassengerGroup* outHere = nullptr;
    bool somethingHere = false;
    for (PassengerGroup& g : groups_) {
      if (g.carrier != 1 || g.done || glm::distance(at, g.point) > kZoneRadius) continue;
      somethingHere = true;
      if (g.out && !outHere) outHere = &g;
      if (!g.out) call(g, heli, targets);
    }
    // Troops out, one every second or so.
    if (outHere && (nextOut_ -= dt) <= 0.0f) {
      int unit = -1;
      if (!outHere->units.empty()) {
        unit = outHere->units.front();
        outHere->units.erase(outHere->units.begin());
      } else {
        --outHere->men;
      }
      if (unit < 0 || !targets.list[size_t(unit)].destroyed) {
        addOut(heli, terrain, Walker::Kind::Soldier, true, unit);
        radio_.push_back(snd::FeetOut);
        ++deployedMen_;
        troops_ = std::max(0, troops_ - 1);
      }
      nextOut_ = 1.3f;
      if (outHere->units.empty() && outHere->men <= 0) {
        outHere->done = true;
        radio_.push_back(snd::TroopDeployed);
      }
    }
    // Wrong place: said once a door opening, if nobody got out or in.
    if (somethingHere || coming_) wrongSaid_ = true;
    if (!wrongSaid_) {
      bool outLeft = false, inLeft = false;
      for (const PassengerGroup& g : groups_)
        if (g.carrier == 1 && !g.done) (g.out ? outLeft : inLeft) = true;
      const bool atHome = glm::distance(at, home_) < 2000.0f;
      if ((outLeft || inLeft) && !atHome) {
        radio_.push_back(outLeft ? snd::WrongDeploymentArea : snd::WrongBoardingArea);
        wrongSaid_ = true;
      }
    }
    // Home again: the casualties are taken out.
    if (casualties_ > 0 && glm::distance(at, home_) < 2000.0f && (nextOut_ -= dt) <= 0.0f) {
      addOut(heli, terrain, Walker::Kind::Stretcher, false, -1);
      nextOut_ = 3.0f;
      if (--casualties_ == 0) radio_.push_back(snd::CasualtiesRemoved);
    }
  }

  // The men on foot.
  const bool sitting = landed(heli);
  int coming = 0;
  for (Walker& w : walkers_) {
    const float speed = w.kind == Walker::Kind::Stretcher ? kCarry : kWalk;
    if (w.wait > 0.0f) {
      w.wait = std::max(0.0f, w.wait - dt);
      if (w.goal == Walker::Goal::In) ++coming;
      continue;
    }
    if (w.hop > 0.0f) {
      if (w.goal == Walker::Goal::In) ++coming;
      // Between the floor and the ground: along a little arc.
      w.hop = std::min(1.0f, w.hop + dt / kHopTime);
      w.pos = glm::mix(w.from, w.to, w.hop) + glm::vec3(0.0f, 0.5f * std::sin(w.hop * 3.14159f), 0.0f);
      if (w.hop >= 1.0f) {
        w.hop = 0.0f;
        if (w.goal == Walker::Goal::In) {
          --coming;
          w.gone = true;
          loadedMen_ += w.men;
          if (w.kind == Walker::Kind::Stretcher) ++casualties_;
          else ++rescued_;
          radio_.push_back(snd::FeetIn);
        } else {
          w.from = w.pos;
          w.to = w.dest;
        }
      }
      continue;
    }
    if (w.goal == Walker::Goal::In) {
      ++coming;
      if (!sitting) continue;  // the helicopter is not there to climb into
    }
    if (w.goal == Walker::Goal::Stand) continue;
    glm::vec3 d = w.to - w.pos;
    d.y = 0.0f;
    const float left = glm::length(d);
    if (left < 0.05f) {
      if (w.goal == Walker::Goal::In) {
        // At the door: up into the cabin.
        w.from = w.pos;
        w.to = w.cabin;
        w.hop = 1e-3f;
      } else if (w.unit >= 0) {
        // Out and away: the unit of the mission again, on along its route.
        Vehicle& v = targets.list[size_t(w.unit)];
        v.pos = w.pos;
        v.heading = w.heading;
        v.aboard = false;
        w.gone = true;
      } else {
        w.goal = Walker::Goal::Stand;
      }
      continue;
    }
    const float step = std::min(left, speed * dt);
    w.pos += d / left * step;
    w.pos.y = terrain.groundHeight(w.pos.x, w.pos.z);
    w.heading = std::atan2(d.x, -d.z);
    w.stride += step / 0.7f * 3.14159f;
  }
  walkers_.erase(std::remove_if(walkers_.begin(), walkers_.end(), [](const Walker& w) { return w.gone; }), walkers_.end());
  if (coming_ && coming == 0) {
    coming_ = false;
    radio_.push_back(casualties_ > 0 ? snd::CasualtiesAboard : snd::TroopsAboard);
  }
}

void Troops::addOut(const Helicopter& heli, const world::Terrain& terrain, Walker::Kind kind, bool soldierModel, int unit) {
  // From the cabin floor, a hop down by the door, then off some 20 m on that side, fanned out.
  const int side = outSide_ ^= 1;
  const float spread = float(int(walkers_.size()) % 5) - 2.0f;
  auto ground = [&](glm::vec3 p) { return glm::vec3(p.x, terrain.groundHeight(p.x, p.z), p.z); };
  Walker w;
  w.kind = kind;
  w.soldierModel = soldierModel;
  w.unit = unit;
  w.goal = Walker::Goal::Out;
  w.from = door(heli, side, -0.4f);
  w.to = ground(door(heli, side, 1.0f));
  w.dest = ground(heli.pos + heli.orient * glm::vec3((side ? 1.0f : -1.0f) * (18.0f + 3.0f * std::fabs(spread)), 0.0f,
                                                      kDoorZ + 6.0f * spread));
  w.pos = w.from;
  w.heading = headingTo(w.from, w.to);
  w.hop = 1e-3f;
  walkers_.push_back(w);
}

void Troops::addIn(const Helicopter& heli, glm::vec3 from, Walker::Kind kind) {
  // Over to the nearer door, then a hop up into the cabin; one after another.
  const glm::vec3 local = glm::inverse(heli.orient) * (from - heli.pos);
  const int side = local.x > 0.0f ? 1 : 0;
  Walker w;
  w.kind = kind;
  w.soldierModel = false;
  w.goal = Walker::Goal::In;
  w.pos = from;
  w.to = door(heli, side, 1.0f);
  w.to.y = from.y;
  w.cabin = door(heli, side, -0.4f);
  w.heading = headingTo(from, w.to);
  w.wait = 0.8f * float(walkers_.size());
  walkers_.push_back(w);
}

}  // namespace sim
