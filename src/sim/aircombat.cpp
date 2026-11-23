#include "sim/aircombat.hpp"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

glm::vec2 flat(glm::vec3 p) { return {p.x, p.z}; }

}  // namespace

bool AirCombat::sees(glm::vec3 from, glm::vec3 to) const {
  const glm::vec3 d = to - from;
  const int steps = std::max(1, int(glm::length(glm::vec2(d.x, d.z)) / 60.0f));
  for (int i = 1; i < steps; ++i) {
    const glm::vec3 p = from + d * (float(i) / float(steps));
    if (p.y < terrain_.groundHeight(p.x, p.z)) return false;
  }
  return true;
}

void AirCombat::step(Targets& targets, const std::vector<const Helicopter*>& helis, float dt) {
  if (units_.size() != targets.list.size()) units_.resize(targets.list.size());
  auto fair = [](const Helicopter* h) { return h && !h->crashed && !h->damage.shotDown && h->weightOnWheels <= 0.0f; };
  for (size_t i = 0; i < targets.list.size(); ++i) {
    Vehicle& v = targets.list[i];
    const VehicleType& t = vehicleType(v.kind);
    const bool fighter = t.arms == Arms::Fighter, gunship = t.arms == Arms::Gunship;
    if ((!fighter && !gunship) || v.side != Side::Enemy || v.destroyed || v.aboard || !v.armed) continue;
    Unit& u = units_[i];
    const float ground = terrain_.groundHeight(v.pos.x, v.pos.z);
    const bool airborne = v.pos.y - ground > 5.0f;
    // (Standing on an airfield it stays there: no scramble.)
    if (!airborne && !u.engaged) continue;
    const float reach = fighter ? kFighterReach : kGunshipReach;

    // Once a second: the nearest of ours it sees within its reach (a little further once at it).
    u.look -= dt;
    if (u.look <= 0.0f && enabled) {
      u.look = 1.0f;
      int pick = -1;
      float best = (u.engaged ? 1.5f : 1.0f) * reach;
      for (size_t k = 0; k < helis.size(); ++k) {
        if (!fair(helis[k])) continue;
        const float d = glm::distance(v.pos, helis[k]->pos);
        if (d < best && sees(v.pos, helis[k]->pos)) best = d, pick = int(k);
      }
      if (pick >= 0 && !u.engaged) {
        u.engaged = true;
        u.plan = v.route, u.planNext = v.next, u.planLoop = v.loop;
        u.extend = 0.0f;
      }
      if (pick < 0 && u.engaged) {
        // Nobody to fight: back on its route where it left it.
        u.engaged = false;
        v.route = u.plan, v.next = std::min(u.planNext, u.plan.size()), v.loop = u.planLoop;
        v.hover = false;
        if (v.route.empty()) v.route = {{flat(v.pos), 150.0f, 100.0f, 0.0f}}, v.next = 1;
        continue;
      }
      u.target = pick;
    }
    if (!enabled) continue;
    if (!u.engaged || u.target < 0 || size_t(u.target) >= helis.size() || !fair(helis[size_t(u.target)])) continue;
    const Helicopter& h = *helis[size_t(u.target)];
    const glm::vec2 at = flat(v.pos), to = flat(h.pos) - at;
    const float d = glm::length(to);
    const float hAgl = h.pos.y - terrain_.groundHeight(h.pos.x, h.pos.z);
    std::vector<RoutePoint> route{{at, v.pos.y - ground, 1.0f, 0.0f}};
    if (fighter) {
      // Passes: at it from behind its line, through, out for some seconds, round again.
      const glm::vec2 vel = flat(v.vel);
      const glm::vec2 heading = glm::length(vel) > 1.0f ? glm::normalize(vel) : glm::vec2(std::sin(v.heading), -std::cos(v.heading));
      if (u.extend <= 0.0f && d < 300.0f) u.extend = 7.0f, u.away = heading;
      const float alt = std::max(hAgl + 40.0f, 90.0f);
      if (u.extend > 0.0f) {
        u.extend -= dt;
        route.push_back({at + u.away * 2000.0f, alt + 150.0f, 150.0f, 0.0f});
      } else {
        const glm::vec2 lead = flat(h.pos) + flat(h.vel) * std::min(3.0f, d / 150.0f);
        const glm::vec2 aim = glm::distance(lead, at) < 400.0f ? at + heading * 600.0f : lead;
        route.push_back({aim, alt, d > 2500.0f ? 170.0f : 130.0f, 0.0f});
      }
    } else {
      // The gunship: in to its gun's range, then holding off there, its nose round on it.
      const glm::vec2 dir = d > 1.0f ? to / d : glm::vec2(0.0f, -1.0f);
      const float alt = std::clamp(hAgl, 25.0f, 120.0f);
      if (d > 1400.0f) route.push_back({at + dir * std::min(d - 900.0f, 3000.0f) + dir * 200.0f, alt, 55.0f, 0.0f});
      else if (d > 700.0f) route.push_back({at + dir * 200.0f, alt, 1.0f, 0.0f});
      else route.push_back({at - dir * 300.0f + glm::vec2(-dir.y, dir.x) * 200.0f, alt, 15.0f, 0.0f});
      v.hover = true;
    }
    v.route = route, v.next = 1, v.loop = false, v.waiting = v.delay = 0.0f;
  }
}

}  // namespace sim
