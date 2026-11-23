// Headless checks of the flight model against figures from the original manual.
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "sim/heli.hpp"
#include "sim/aircombat.hpp"
#include "sim/bowman.hpp"
#include "sim/battle.hpp"
#include "sim/enemy.hpp"
#include "gamedata/anim.hpp"
#include "gamedata/campaign.hpp"
#include "gamedata/cd.hpp"
#include "gamedata/mission.hpp"
#include "gamedata/original.hpp"
#include "gamedata/picture.hpp"
#include "gamedata/sound.hpp"
#include "gamedata/zone.hpp"
#include "net/link.hpp"
#include "net/mirror.hpp"
#include "platform/door_gun.hpp"
#include "render/daylight.hpp"
#include "sim/mission.hpp"
#include "sim/orders.hpp"
#include "sim/pilot_log.hpp"
#include "sim/topten.hpp"
#include "sim/spotter.hpp"
#include "sim/targets.hpp"
#include "sim/threats.hpp"
#include "sim/debris.hpp"
#include "sim/troops.hpp"
#include "sim/weapons.hpp"
#include "sim/wing_arms.hpp"
#include "sim/wso.hpp"
#include "world/terrain.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what, double value) {
  std::printf("%s %-58s %.3f\n", ok ? "[ OK ]" : "[FAIL]", what, value);
  if (!ok) ++failures;
}

constexpr float kDt = 1.0f / 240.0f;

void run(sim::Helicopter& h, const sim::Controls& c, float seconds) {
  for (int i = 0; i < int(seconds / kDt); ++i) h.step(c, kDt);
}


// A simple scripted pilot that only moves the controls, like a player would.
struct Pilot {
  const world::Terrain& terrain;
  float pitchTarget = 0, rollTarget = 0, headingTarget = 0, altTarget = 0;
  float vsTarget = 0;   // used when altTarget < 0
  bool stop = false;    // pitch to kill the forward speed
  sim::Controls cmd(const sim::Helicopter& h) const {
    sim::Controls c;
    float pt = pitchTarget;
    if (stop) {
      float vf = glm::dot(h.vel, h.forward());
      pt = std::clamp(vf * 1.5f, -8.0f, 10.0f);
    }
    float dPitch = pt - h.pitchDeg();
    c.cyclicY = std::clamp(-0.06f * dPitch + 1.5f * h.omega.x, -1.0f, 1.0f);
    float dRoll = rollTarget - h.rollDeg();
    c.cyclicX = std::clamp(0.05f * dRoll + 0.8f * h.omega.z, -1.0f, 1.0f);
    float dHdg = std::remainder(headingTarget - h.headingDeg(), 360.0f);
    c.pedal = std::clamp(0.05f * dHdg + 1.5f * h.omega.y, -1.0f, 1.0f);
    float base = sim::Helicopter::levelFlightCollective(glm::length(h.vel), h.mass);
    // Look ahead like a pilot watching the terrain.
    float agl = h.radarAltitude();
    for (float t : {1.0f, 2.0f, 3.0f}) {
      glm::vec3 q = h.pos + h.vel * t;
      agl = std::min(agl, h.pos.y - sim::Helicopter::kRestHeight - terrain.groundHeight(q.x, q.z));
    }
    float vs = altTarget >= 0 ? std::clamp(0.4f * (altTarget - agl), -6.0f, 10.0f) : vsTarget;
    c.collective = std::clamp(base + 0.08f * (vs - h.vel.y), 0.0f, 1.3f);
    if (altTarget < 0 && agl < 1.0f) c.collective -= 0.15f;               // ground cushion: lower the lever
    if (altTarget < 0 && h.weightOnWheels > 0.02f) c.collective = 0.0f;  // down: lever fully down
    c.collective = std::max(c.collective, 0.0f);
    return c;
  }
};

// Take off, accelerate to near Vmax, decelerate, hover and land.
void flyCircuit(const world::Terrain& terrain, sim::FlightModel model, const char* name) {
  sim::Helicopter h(terrain);
  h.model = model;
  h.resetOnGround(terrain.airfield().apronCenter, 0.0f);
  h.brakes = false;
  Pilot p{terrain};
  p.altTarget = 40.0f;
  float t = 0, maxKmh = 0, minCruiseAgl = 1e9f;
  auto fly = [&](float maxTime, auto until) {
    for (float tt = 0; tt < maxTime && !h.crashed; tt += kDt, t += kDt) {
      h.step(p.cmd(h), kDt);
      if (until()) return true;
    }
    return false;
  };
  bool ok = fly(30.0f, [&] { return h.radarAltitude() > 30.0f; });
  if (ok && h.gearDown) h.toggleGear();
  p.pitchTarget = -12.0f;
  bool fast = ok && fly(120.0f, [&] {
    maxKmh = std::max(maxKmh, glm::length(h.vel) * 3.6f);
    if (glm::length(h.vel) > 15.0f) minCruiseAgl = std::min(minCruiseAgl, h.radarAltitude());
    return glm::length(h.vel) * 3.6f > 290.0f;
  });
  p.pitchTarget = 12.0f;
  bool slow = fast && fly(90.0f, [&] { return glm::length(h.vel) * 3.6f < 25.0f; });
  p.stop = true;
  slow = slow && fly(40.0f, [&] { return glm::length(h.vel) < 1.0f; });
  if (slow && !h.gearDown) h.toggleGear();
  p.altTarget = -1.0f;
  p.vsTarget = -0.8f;
  bool landed = slow && fly(120.0f, [&] { return h.weightOnWheels > 0.9f; });
  char label[96];
  std::snprintf(label, sizeof label, "%s: take-off and climb to 30 m", name);
  check(ok && !h.crashed, label, t);
  std::snprintf(label, sizeof label, "%s: accelerate to 290 km/h", name);
  check(fast, label, maxKmh);
  std::snprintf(label, sizeof label, "%s: stay above 15 m while accelerating", name);
  check(minCruiseAgl > 15.0f, label, minCruiseAgl);
  std::snprintf(label, sizeof label, "%s: flare back to the hover", name);
  check(slow, label, glm::length(h.vel) * 3.6f);
  std::snprintf(label, sizeof label, "%s: vertical landing without damage", name);
  check(landed && !h.crashed, label, h.weightOnWheels);
}

}  // namespace

// Level flight at a nose attitude (Stable): the speed it settles at and the collective that holds
// the height there, at `height` m above sea level (density) far off the ground. For the manual's
// chart (tools/collective_chart.py).
struct Trim {
  float kmh, collective;
};
static Trim trimAt(const world::Terrain& terrain, float pitchDeg, float mass, float height, float seconds) {
  sim::Helicopter h(terrain);
  const float theta = glm::radians(pitchDeg);
  const glm::vec3 at{0.0f, height, 0.0f};
  const float kmh0 = std::max(0.0f, -pitchDeg * 30.0f + 80.0f);
  h.resetAirborne(at, 0.0f, kmh0 / 3.6f);
  h.model = sim::FlightModel::Stable;
  h.noCrash = true, h.infiniteFuel = true;
  sim::Controls in;
  in.collective = sim::Helicopter::levelFlightCollective(kmh0 / 3.6f, mass);
  const float dt = 1.0f / 120.0f;
  float sumC = 0.0f, sumV = 0.0f;
  int n = 0;
  for (float t = 0.0f; t < seconds; t += dt) {
    const glm::vec3 f = h.forward();
    const float pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    in.cyclicY = std::clamp((pitch - theta) * 1.5f, -1.0f, 1.0f);
    in.collective = std::clamp(in.collective - std::clamp(h.vel.y * 0.05f, -0.2f, 0.2f) * dt, 0.0f, sim::Helicopter::kMaxCollective);
    h.mass = mass;
    h.step(in, dt);
    h.pos = at;
    if (t > seconds * 0.8f) sumC += in.collective, sumV += glm::dot(h.vel, glm::normalize(glm::vec3(f.x, 0.0f, f.z))), ++n;
  }
  return {sumV / float(n) * 3.6f, sumC / float(n) * 100.0f};
}

// The manual's autorotation at 9.85 t over the airfield: disengaged at 900 m, a glide at 150 km/h
// and 45 %, the nose up at `flareAt` m, level again at 12 m with the lever following the sink.
struct Autorotation {
  bool landed;
  std::string crash;
  float glideSink, glideRpm, touchSink, seconds, distance;
};
static Autorotation autorotate(const world::Terrain& terrain, sim::FlightModel model, float flareAt) {
  sim::Helicopter h(terrain);
  const float ground = terrain.groundHeight(0.0f, 0.0f);
  h.resetAirborne({0.0f, ground + 900.0f, 0.0f}, 0.0f, 200.0f / 3.6f);
  h.model = model;
  h.mass = 9850.0f;
  h.gearDown = true, h.gearPos = 1.0f;
  h.toggleEngines();
  sim::Controls in;
  in.collective = 0.6f;
  Autorotation out{false, "", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const float dt = 1.0f / 120.0f;
  float attitude = -2.0f;
  bool touched = false;
  for (; out.seconds < 200.0f && !h.crashed; out.seconds += dt) {
    const float agl = h.radarAltitude();
    const glm::vec3 f = h.forward();
    const glm::vec3 side{-f.z, 0.0f, f.x};
    float pitch, lever = h.rotorRpm < 100.0f && agl > flareAt ? 0.35f : 0.45f;
    if (agl > flareAt) {
      attitude = std::clamp(attitude + std::clamp((glm::dot(h.vel, f) * 3.6f - 150.0f) * 0.02f, -0.8f, 0.8f) * dt, -20.0f, 20.0f);
      pitch = attitude;
    } else if (agl > 12.0f) {
      pitch = 20.0f;
    } else {
      pitch = touched ? 0.0f : std::clamp(agl * 0.6f, 0.0f, 20.0f);
      lever = touched ? 0.0f : std::clamp(0.45f + (-h.vel.y - 1.5f) * 0.12f, 0.0f, 1.0f);
    }
    in.cyclicY = std::clamp((h.pitchDeg() - pitch) * 0.3f, -1.0f, 1.0f);
    in.cyclicX = std::clamp(-h.rollDeg() * 0.2f, -1.0f, 1.0f);
    in.pedal = std::clamp(glm::dot(h.vel, side) * 0.5f, -1.0f, 1.0f);
    in.collective += std::clamp(lever - in.collective, -dt, dt);
    if (!touched && h.weightOnWheels > 0.0f) touched = true, out.touchSink = -h.vel.y;
    if (!touched) out.distance = -h.pos.z;
    if (agl > 100.0f && agl < 150.0f) out.glideSink = -h.vel.y, out.glideRpm = h.rotorRpm;
    h.step(in, dt);
    if (touched && glm::length(h.vel) < 1.0f) break;
  }
  out.landed = touched && !h.crashed;
  out.crash = h.crashCause;
  return out;
}

// The manual's braking onto a Shahed: from 300 km/h at 9.85 t, 60 % collective and the nose at +10
// degrees until 185 km/h; then what holding -3 degrees takes on the stick.
struct Flare {
  float seconds, climb, stick;
};
static Flare flareOntoShahed(const world::Terrain& terrain, sim::FlightModel model) {
  sim::Helicopter h(terrain);
  const glm::vec3 at{0.0f, 450.0f, 0.0f};
  h.resetAirborne(at, 0.0f, 300.0f / 3.6f);
  h.model = model;
  h.noCrash = true, h.infiniteFuel = true;
  h.mass = 9850.0f;
  sim::Controls in;
  in.collective = 0.83f;
  const float dt = 1.0f / 120.0f;
  const auto fly = [&](float pitch) {
    const glm::vec3 f = h.forward();
    const glm::vec3 side{-f.z, 0.0f, f.x};
    in.cyclicY = std::clamp((h.pitchDeg() - pitch) * 0.3f, -1.0f, 1.0f);
    in.cyclicX = std::clamp(-h.rollDeg() * 0.2f, -1.0f, 1.0f);
    in.pedal = std::clamp(glm::dot(h.vel, side) * 0.5f, -1.0f, 1.0f);
  };
  for (float t = 0.0f; t < 100.0f; t += dt) {
    fly(-8.3f);
    in.collective = std::clamp(in.collective - std::clamp(h.vel.y * 0.05f, -0.2f, 0.2f) * dt, 0.0f, sim::Helicopter::kMaxCollective);
    h.step(in, dt);
    h.pos = at;
  }
  Flare out{0.0f, 0.0f, 0.0f};
  while (out.seconds < 60.0f && glm::dot(h.vel, h.forward()) * 3.6f > 185.0f) {
    fly(10.0f);
    in.collective += std::clamp(0.6f - in.collective, -0.5f * dt, 0.5f * dt);
    h.step(in, dt);
    out.seconds += dt;
  }
  out.climb = h.pos.y - at.y;
  int n = 0;
  for (float t = 0.0f; t < 10.0f; t += dt) {
    fly(-3.0f);
    in.collective += std::clamp(0.55f - h.vel.y * 0.02f - in.collective, -0.5f * dt, 0.5f * dt);
    h.step(in, dt);
    if (t > 5.0f) out.stick += in.cyclicY, ++n;
  }
  out.stick /= float(n);
  return out;
}

int main(int argc, char** argv) {
  using sim::FlightModel;
  using sim::Helicopter;
  world::Terrain terrain;
  const auto& af = terrain.airfield();
  // --trim-table: what tools/collective_chart.py draws the manual's chart from.
  if (argc > 1 && std::string(argv[1]) == "--trim-table") {
    std::printf("mass,height,pitch,kmh,collective\n");
    for (const auto& [mass, height] : {std::pair{9850.0f, 450.0f}, std::pair{11000.0f, 450.0f}, std::pair{11700.0f, 450.0f}, std::pair{11000.0f, 1500.0f}})
      for (float pitch : {0.0f, -0.1f, -0.2f, -0.3f, -0.4f, -0.5f, -0.6f, -0.7f, -0.8f, -0.9f, -1.0f, -1.5f, -2.0f, -3.0f, -4.0f, -5.0f, -6.0f, -7.0f, -8.0f, -9.0f, -10.0f, -11.0f, -12.0f}) {
        const Trim t = trimAt(terrain, pitch, mass, height, 400.0f);
        std::printf("%.0f,%.0f,%.1f,%.1f,%.1f\n", mass, height, pitch, t.kmh, t.collective);
      }
    return 0;
  }

  std::printf("Level flight collective curve (sea level, 11 t):\n");
  for (float kmh : {0.f, 50.f, 100.f, 135.f, 200.f, 250.f, 300.f, 335.f, 360.f}) {
    float pitch;
    float c = Helicopter::levelFlightCollective(kmh / 3.6f, Helicopter::kRefMass, &pitch);
    std::printf("  %5.0f km/h  collective %5.1f %%  pitch %5.1f deg\n", kmh, c * 100.f, -pitch * 57.2958f);
  }
  {
    // The manual's chart (docs/collective.csv) against the simulation: when this fails, the flight
    // physics changed: run tools/collective_chart.py.
    const std::string source = __FILE__;
    std::ifstream chart(source.substr(0, source.rfind("tests")) + "docs/collective.csv");
    std::vector<std::pair<float, float>> curve;  // 11 t at 450 m
    std::string line;
    while (std::getline(chart, line)) {
      int mass = 0, height = 0;
      float kmh = 0.0f, c = 0.0f;
      if (std::sscanf(line.c_str(), "%d,%d,%f,%f", &mass, &height, &kmh, &c) == 4 && mass == 11000 && height == 450) curve.emplace_back(kmh, c);
    }
    if (curve.size() < 5) {
      std::printf("[SKIP] docs/collective.csv not found\n");
    } else {
      float worst = 0.0f;
      for (float pitch : {-1.0f, -4.0f, -8.0f}) {
        const Trim t = trimAt(terrain, pitch, 11000.0f, 450.0f, 300.0f);
        for (size_t k = 0; k + 1 < curve.size(); ++k)
          if (curve[k].first <= t.kmh && t.kmh <= curve[k + 1].first) {
            const float u = (t.kmh - curve[k].first) / std::max(curve[k + 1].first - curve[k].first, 0.001f);
            worst = std::max(worst, std::fabs(t.collective - (curve[k].second + (curve[k + 1].second - curve[k].second) * u)));
          }
      }
      check(worst < 1.5f, "the manual's collective chart fits the flight model (else: tools/collective_chart.py) (%)", worst);
    }
  }
  {
    // The figures of the manual's "Matching speed with a Shahed": when these fail, the physics
    // changed: measure them anew and correct README.md and README.en.md.
    const Flare s = flareOntoShahed(terrain, FlightModel::Stable), r = flareOntoShahed(terrain, FlightModel::Realistic);
    check(s.seconds > 10.0f && s.seconds < 14.0f, "manual: braking onto a Shahed from 300 km/h takes about 12 s (s)", s.seconds);
    check(s.climb > 25.0f && s.climb < 55.0f, "manual: and at 60 % it climbs some 40 m (m)", s.climb);
    check(std::fabs(s.stick) < 0.02f, "manual: Stable holds the nose with the stick centred", s.stick);
    check(r.seconds > 10.0f && r.seconds < 14.0f, "manual: Realistic brakes the same (s)", r.seconds);
    check(r.stick > 0.09f && r.stick < 0.15f, "manual: Realistic takes 12 % forward stick at 180 km/h", r.stick);
  }
  {
    // Autorotation: the figures of the manual and the drill's briefing.
    for (FlightModel m : {FlightModel::Stable, FlightModel::Realistic}) {
      const Autorotation a = autorotate(terrain, m, 50.0f);
      check(a.landed, "autorotation: the manual's way lands", a.touchSink);
      std::printf("  autorotation from 900 m: %.0f s to rest, %.0f m to the touchdown, touching at %.1f m/s\n", a.seconds, a.distance, a.touchSink);
      check(a.glideSink > 13.0f && a.glideSink < 22.0f, "autorotation: the glide sinks 15-20 m/s", a.glideSink);
      check(a.distance > 2300.0f && a.distance < 3700.0f, "autorotation: 900 m carries it some 3 km", a.distance);
      check(a.glideRpm > 95.0f && a.glideRpm < 115.0f, "autorotation: and the rotor keeps its revolutions (%)", a.glideRpm);
    }
    const Autorotation noFlare = autorotate(terrain, FlightModel::Stable, 0.0f);
    check(!noFlare.landed, "autorotation: without the flare it does not end well", noFlare.touchSink);
    Helicopter h(terrain);
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 2000.0f, 0.0f}, 0.0f, 200.0f / 3.6f);
    h.model = FlightModel::Stable;
    h.mass = 9850.0f;
    h.toggleEngines();
    sim::Controls in;
    in.collective = 0.6f;
    for (float t = 0.0f; t < 5.0f; t += 1.0f / 120.0f) h.step(in, 1.0f / 120.0f);
    check(h.rotorFree && h.rotorRpm < 75.0f && h.torque == 0.0f, "autorotation: the lever left up spends the revolutions in seconds", h.rotorRpm);
    h.toggleEngines();
    for (float t = 0.0f; t < 8.0f; t += 1.0f / 120.0f) h.step(in, 1.0f / 120.0f);
    check(!h.rotorFree && h.rotorRpm > 97.0f && h.collective > 0.59f, "autorotation: engaged again, the engines bring it back", h.rotorRpm);
    h.model = FlightModel::Novice;
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 2000.0f, 0.0f}, 0.0f, 50.0f);
    h.toggleEngines();
    check(!h.enginesIdle, "autorotation: not in Novice", h.enginesIdle ? 1.0f : 0.0f);
  }
  float cHover = Helicopter::levelFlightCollective(0.0f, Helicopter::kRefMass);
  float cEtl = Helicopter::levelFlightCollective(135.0f / 3.6f, Helicopter::kRefMass);
  float cCruise = Helicopter::levelFlightCollective(300.0f / 3.6f, Helicopter::kRefMass);
  float cMax = Helicopter::levelFlightCollective(335.0f / 3.6f, Helicopter::kRefMass);
  check(cHover > 0.74f && cHover < 0.84f, "hover OGE collective 74-84 %", cHover);
  check(cEtl < cHover - 0.12f, "translational lift: 135 km/h needs much less", cEtl);
  check(cCruise > 0.75f && cCruise < 0.95f, "cruise 300 km/h collective 75-95 %", cCruise);
  check(cMax > 0.93f && cMax < 1.07f, "Vmax 335 km/h reached at ~100 %", cMax);

  // Parked: nothing moves, nothing breaks.
  {
    Helicopter h(terrain);
    h.resetOnGround(af.apronCenter, 0.0f);
    glm::vec3 p0 = h.pos;
    run(h, {}, 30.0f);
    check(!h.crashed && glm::length(h.pos - p0) < 0.5f, "parked 30 s: stays put", glm::length(h.pos - p0));
  }

  // On the ground with the brakes set the pedals do not turn the helicopter.
  for (FlightModel m : {FlightModel::Novice, FlightModel::Stable, FlightModel::Realistic}) {
    Helicopter h(terrain);
    h.model = m;
    h.resetOnGround(af.apronCenter, 0.0f);
    run(h, {}, 2.0f);
    sim::Controls c;
    c.collective = m == FlightModel::Novice ? 0.5f : 0.3f;
    c.pedal = 1.0f;
    float hdg0 = h.headingDeg();
    run(h, c, 5.0f);
    float turned = std::fabs(std::remainder(h.headingDeg() - hdg0, 360.0f));
    char label[96];
    std::snprintf(label, sizeof label, "%s: braked, full pedal does not turn (deg)", sim::flightModelName(m));
    check(turned < 2.0f && !h.crashed, label, turned);
  }

  // Stall turn: nose 70 deg up, full pedal yaws about the rotor mast, so the nose swings down
  // through the horizon instead of the helicopter spinning on its tail about the vertical; well
  // down (in Stable too: its wings are not levelled on the way, through the steep bank).
  for (FlightModel m : {FlightModel::Stable, FlightModel::Realistic}) {
    Helicopter h(terrain);
    h.model = m;
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 800.0f, 0.0f}, 0.0f, 20.0f);
    h.orient = glm::angleAxis(glm::radians(70.0f), glm::vec3(1, 0, 0));
    h.omega = glm::vec3(0.0f);
    sim::Controls c;
    c.collective = 0.6f;
    c.pedal = 1.0f;
    float lowest = h.pitchDeg();
    for (int i = 0; i < 500; ++i) {
      h.step(c, 1.0f / 100.0f);
      lowest = std::min(lowest, h.pitchDeg());
    }
    char label[96];
    std::snprintf(label, sizeof label, "%s: stall turn, full pedal nose 70 up brings the nose down (deg)", sim::flightModelName(m));
    check(lowest < -55.0f && !h.crashed, label, lowest);
  }

  // Taxi: brakes off, collective ~50 %, forward cyclic rolls the helicopter along the ground.
  for (FlightModel m : {FlightModel::Novice, FlightModel::Stable, FlightModel::Realistic}) {
    Helicopter h(terrain);
    h.model = m;
    h.resetOnGround(af.apronCenter, 0.0f);
    run(h, {}, 2.0f);
    h.brakes = false;
    glm::vec3 p0 = h.pos;
    sim::Controls c;
    c.collective = 0.5f;
    c.cyclicY = 1.0f;
    float minWow = 1.0f;
    for (int i = 0; i < int(10.0f / kDt); ++i) {
      h.step(c, kDt);
      minWow = std::min(minWow, h.weightOnWheels);
    }
    float moved = glm::dot(h.pos - p0, h.forward());
    char label[96];
    std::snprintf(label, sizeof label, "%s: taxi 10 s forward (m)", sim::flightModelName(m));
    check(moved > 15.0f && !h.crashed, label, moved);
    std::snprintf(label, sizeof label, "%s: taxi stays on the wheels", sim::flightModelName(m));
    check(minWow > 0.1f, label, minWow);
    // Brakes stop it again.
    h.brakes = true;
    c.cyclicY = 0.0f;
    c.collective = m == FlightModel::Novice ? 0.5f : 0.3f;
    run(h, c, 6.0f);
    std::snprintf(label, sizeof label, "%s: brakes stop the taxi (m/s)", sim::flightModelName(m));
    check(glm::length(h.vel) < 0.3f, label, glm::length(h.vel));
  }

  // Lift-off torque on the ground (manual: typically 70-80 %).
  {
    Helicopter h(terrain);
    h.model = FlightModel::Stable;
    h.resetOnGround(af.apronCenter, 0.0f);
    run(h, {}, 2.0f);
    sim::Controls c;
    float liftoff = -1.0f;
    for (c.collective = 0.5f; c.collective < 1.0f; c.collective += 0.02f * kDt) {
      h.step(c, kDt);
      if (h.radarAltitude() > 0.5f) {
        liftoff = c.collective;
        break;
      }
    }
    check(liftoff > 0.66f && liftoff < 0.80f, "stable: lift-off collective 66-80 %", liftoff);
    check(!h.crashed, "stable: lift-off without crash", h.crashed);
  }

  // Hover out of ground effect with the computed hover collective holds height.
  {
    Helicopter h(terrain);
    h.model = FlightModel::Stable;
    glm::vec3 start{af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 100.0f, af.center.y};
    h.resetAirborne(start, 0.0f, 0.0f);
    sim::Controls c;
    float sigma = std::exp(-start.y / 8500.0f);
    c.collective = cHover / sigma;
    run(h, c, 10.0f);
    check(std::fabs(h.pos.y - start.y) < 1.5f, "stable: hover holds height 10 s (+-1.5 m)", h.pos.y - start.y);
  }

  // Vertical climb from the hover at 100 % (manual: ~500 ft/min = 2.5 m/s).
  {
    Helicopter h(terrain);
    h.model = FlightModel::Stable;
    glm::vec3 start{af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 100.0f, af.center.y};
    h.resetAirborne(start, 0.0f, 0.0f);
    sim::Controls c;
    c.collective = 1.0f;
    run(h, c, 15.0f);
    check(h.vel.y > 1.8f && h.vel.y < 4.0f, "stable: vertical climb at 100 % 1.8-4 m/s", h.vel.y);
  }

  // Novice: collective 50 % = hover.
  {
    Helicopter h(terrain);
    h.model = FlightModel::Novice;
    glm::vec3 start{af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 50.0f, af.center.y};
    h.resetAirborne(start, 0.0f, 0.0f);
    sim::Controls c;
    c.collective = 0.5f;
    run(h, c, 20.0f);
    check(std::fabs(h.vel.y) < 0.3f && std::fabs(h.radarAltitude() - 50.0f) < 3.0f,
          "novice: 50 % holds height", h.radarAltitude());
    // Full forward cyclic accelerates without descending.
    c.cyclicY = 1.0f;
    run(h, c, 8.0f);
    c.cyclicY = 0.0f;
    run(h, c, 25.0f);
    float kmh = glm::length(h.vel) * 3.6f;
    check(kmh > 150.0f && !h.crashed, "novice: forward cyclic accelerates", kmh);
    check(h.radarAltitude() > 20.0f, "novice: keeps height above ground", h.radarAltitude());
    // Landing with 30 % collective does not crash.
    c.cyclicY = -1.0f;
    run(h, c, 8.0f);
    c.cyclicY = 0.0f;
    run(h, c, 20.0f);
    c.collective = 0.3f;
    run(h, c, 40.0f);
    check(!h.crashed && h.weightOnWheels > 0.5f, "novice: gentle landing at 30 %", h.weightOnWheels);
  }

  // Weapons: hover 60 m above the airfield, nose north.
  {
    auto hover = [&] {
      auto h = std::make_unique<Helicopter>(terrain);
      glm::vec3 start{af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 60.0f, af.center.y};
      h->resetAirborne(start, 0.0f, 0.0f);
      h->noCrash = true;
      return h;
    };
    std::vector<sim::Impact> impacts;
    auto fly = [&](Helicopter& h, sim::Weapons& w, bool trigger, float seconds) {
      for (int i = 0; i < int(seconds / kDt); ++i) w.step(h, trigger, kDt, impacts);
    };
    {
      auto h = hover();
      sim::Weapons w(terrain);
      w.load({});
      check(w.status() == "NAV" && w.rounds(sim::WeaponType::S8) == 80 && w.rounds(sim::WeaponType::YakB) == 1470,
            "weapons: default load 1470 rounds + 80 S-8, nav mode", w.rounds(sim::WeaponType::S8));
      {
        // E cycles navigation mode, the gun, the rockets and back to navigation mode.
        sim::Weapons c(terrain);
        c.load({});
        std::string seen;
        for (int i = 0; i < 4; ++i, c.selectNext()) seen += c.status().substr(0, 3) + " ";
        check(seen == "NAV YAK S-8 NAV ", "weapons: E cycles NAV, gun, S-8, NAV", 0);
      }
      fly(*h, w, true, 0.5f);
      check(w.shots.empty(), "weapons: safe weapons do not fire", double(w.shots.size()));
      w.selectNext();
      // Turret 30 deg down: one second burst, the rounds land ~100 m ahead.
      w.aimTurret(0.0f, -0.5236f);
      fly(*h, w, false, 2.0f);
      check(std::fabs(w.turretEl() + 0.5236f) < 0.01f, "weapons: turret slews to 30 deg down", w.turretEl());
      impacts.clear();
      fly(*h, w, true, 1.0f);
      int fired = 1470 - w.rounds(sim::WeaponType::YakB);
      check(fired > 60 && fired < 80, "weapons: Yak-B ~4000 rounds/min", fired);
      fly(*h, w, false, 2.0f);
      float sumZ = 0;
      for (auto& i : impacts) sumZ += i.pos.z - h->pos.z;
      float meanAhead = impacts.empty() ? 0.0f : -sumZ / float(impacts.size());
      check(impacts.size() > 50 && meanAhead > 85.0f && meanAhead < 115.0f, "weapons: burst lands ~104 m ahead (m)", meanAhead);
      // Limits: 90 deg left is clamped to 60, 80 deg up to 20.
      w.aimTurret(1.57f, 1.4f);
      fly(*h, w, false, 3.0f);
      check(std::fabs(w.turretAz() - 1.0472f) < 0.01f && std::fabs(w.turretEl() - 0.3491f) < 0.01f,
            "weapons: turret stops at 60 deg left / 20 deg up", glm::degrees(w.turretAz()));
      // Its motors are heard when it swings round, not while it tracks as the airframe turns
      // under it (0.6 rad/s, a brisk pitch on the cyclic).
      bool slewed = false;
      for (int i = 0; i < int(1.0f / kDt); ++i) {
        w.aimTurret(0.5f + 0.6f * kDt * float(i), 0.0f);
        fly(*h, w, false, kDt);
        if (i > int(0.5f / kDt)) slewed = slewed || w.turretSlewing();
      }
      check(!slewed, "weapons: tracking through a turn is quiet", 0);
      w.aimTurret(-0.5f, 0.0f);
      fly(*h, w, false, kDt);
      check(w.turretSlewing(), "weapons: swinging round is heard", 0);
    }
    {
      // Rockets: a pair per press; level flight, the pair lands a few km ahead.
      auto h = hover();
      sim::Weapons w(terrain);
      w.load({});
      w.selectNext();
      w.selectNext();  // no UPK pods loaded: straight to S-8
      check(w.selected() == sim::WeaponType::S8, "weapons: E cycles YAK-B -> S-8 without UPK pods", int(w.selected()));
      impacts.clear();
      fly(*h, w, true, kDt);
      check(w.rounds(sim::WeaponType::S8) == 78, "weapons: one press fires a pair of S-8", w.rounds(sim::WeaponType::S8));
      float maxSpeed = 0;
      for (int i = 0; i < int(2.0f / kDt); ++i) {
        w.step(*h, false, kDt, impacts);
        for (auto& p : w.shots) maxSpeed = std::max(maxSpeed, glm::length(p.vel));
      }
      check(maxSpeed > 500.0f && maxSpeed < 650.0f, "weapons: S-8 burns out at 500-650 m/s", maxSpeed);
      fly(*h, w, false, 20.0f);
      float d = impacts.empty() ? 0.0f : glm::length(glm::vec2(impacts[0].pos.x - h->pos.x, impacts[0].pos.z - h->pos.z));
      check(impacts.size() == 2 && d > 1000.0f, "weapons: S-8 pair fired level lands beyond 1 km (m)", d);
      check(w.pylonRounds(sim::Pylon::Inner) == 38 && w.pylonRounds(sim::Pylon::Outer) == 40,
            "weapons: the pair left the inner pods", w.pylonRounds(sim::Pylon::Inner));
    }
    {
      // Cannon pods on the inner pylons; nothing fires while standing on the wheels.
      Helicopter h(terrain);
      h.resetOnGround(af.apronCenter, 0.0f);
      run(h, {}, 1.0f);
      sim::Weapons w(terrain);
      w.load({sim::Store::Upk23, sim::Store::B8V20});
      w.selectNext();
      w.selectNext();
      check(w.selected() == sim::WeaponType::Upk23 && w.rounds(sim::WeaponType::Upk23) == 500,
            "weapons: UPK-23 pods carry 2 x 250 rounds", w.rounds(sim::WeaponType::Upk23));
      fly(h, w, true, 1.0f);
      check(w.shots.empty() && w.rounds(sim::WeaponType::Upk23) == 500, "weapons: inhibited on the ground",
            double(w.shots.size()));
    }

    {
      // Bombs: FAB-250s inner, released one per press at 200 km/h 100 m up; the bomb falls where
      // the release cue says and wrecks a BMP next to it.
      auto h = hover();
      h->resetAirborne(h->pos + glm::vec3(0, 40.0f, 0), 0.0f, 200.0f / 3.6f);
      sim::Weapons w(terrain);
      w.load({sim::Store::Fab250, sim::Store::B8V20});
      w.selectNext();
      w.selectNext();
      check(w.selected() == sim::WeaponType::Fab250 && w.rounds(sim::WeaponType::Fab250) == 2,
            "weapons: E selects the two FAB-250", w.rounds(sim::WeaponType::Fab250));
      glm::vec3 cue;
      bool reach = w.bombImpact(*h, cue);
      sim::Targets t(terrain);
      int bmp = t.add(sim::VehicleKind::Bmp2, cue.x + 8.0f, cue.z, 0.0f);
      const float mass0 = w.storesMass();
      impacts.clear();
      for (int i = 0; i < int(15.0f / kDt); ++i) w.step(*h, i < 10, kDt, impacts, &t);
      float miss = impacts.empty() ? 1e9f : glm::distance(impacts[0].pos, cue);
      check(reach && miss < 6.0f, "weapons: FAB-250 falls on the release cue (m)", miss);
      check(w.rounds(sim::WeaponType::Fab250) == 1 && std::fabs(mass0 - w.storesMass() - 250.0f) < 1.0f,
            "weapons: one bomb per press, 250 kg lighter", mass0 - w.storesMass());
      check(t.list[size_t(bmp)].destroyed, "weapons: FAB-250 8 m away destroys a BMP-2", t.list[size_t(bmp)].health);
      std::string why;
      check(!sim::storeFits(sim::Store::Fab500, sim::Pylon::Outer, &why) && why == "FAB-500 WON'T FIT ON THE OUTER PYLONS" &&
                sim::storeFits(sim::Store::UB32, sim::Pylon::Inner) && !sim::storeFits(sim::Store::UB32, sim::Pylon::Outer) &&
                sim::storeFits(sim::Store::Shturm, sim::Pylon::Wingtip) &&
                !sim::storeFits(sim::Store::Shturm, sim::Pylon::Inner),
            "weapons: pylon rules of the payload screen", 0.0);
    }

    {
      // 9M114 Shturm from the wingtips: guided along the sight's line it hits a T-62 3 km away
      // (and destroys it); with the sight held 50 m to the side it misses.
      for (bool onTarget : {true, false}) {
        auto h = hover();
        sim::Weapons w(terrain);
        w.load({sim::Store::B8V20, sim::Store::B8V20, sim::Store::Shturm});
        while (w.selected() != sim::WeaponType::Shturm) w.selectNext();
        sim::Targets t(terrain);
        int tank = t.add(sim::VehicleKind::T62, h->pos.x, h->pos.z - 3000.0f, 0.0f);
        const glm::vec3 aim = t.list[size_t(tank)].pos + glm::vec3(onTarget ? 0.0f : 50.0f, 1.2f, 0.0f);
        impacts.clear();
        float flight = 0.0f, tof0 = -1.0f;
        for (int i = 0; i < int(15.0f / kDt) && !t.list[size_t(tank)].destroyed; ++i) {
          w.guide(h->pos, glm::normalize(aim - h->pos));
          w.step(*h, i < 10, kDt, impacts, &t);
          if (i == 10) tof0 = w.missileTimeTo(aim);
          if (!w.shots.empty()) flight += kDt;
        }
        if (onTarget) {
          check(t.list[size_t(tank)].destroyed && flight > 7.0f && flight < 10.0f, "weapons: 9M114 hits a T-62 at 3 km (s)", flight);
          check(tof0 > 6.0f && tof0 < 10.0f && w.rounds(sim::WeaponType::Shturm) == 3, "weapons: 9M114 time to go, 4 on the wingtips",
                tof0);
        } else {
          check(!t.list[size_t(tank)].destroyed, "weapons: 9M114 goes where the sight points", t.list[size_t(tank)].health);
        }
      }
    }

    {
      // Enemy fire at a helicopter hovering 60 m up: a DShK 1 km away hits it within a minute;
      // out of its reach, unarmed or our own it holds its fire.
      sim::VehicleType dshk;
      dshk.name = "DSHK HMG";
      dshk.cls = sim::UnitClass::Vehicle;
      dshk.armour = sim::Armour::Soft;
      dshk.arms = sim::Arms::Hmg;
      const sim::VehicleKind kDshk = sim::registerVehicleType(dshk);
      sim::VehicleType sam7 = dshk;
      sam7.name = "MUJAHEDDIN SAM 7";
      sam7.cls = sim::UnitClass::Soldier;
      sam7.arms = sim::Arms::Manpads;
      const sim::VehicleKind kSam7 = sim::registerVehicleType(sam7);
      std::vector<sim::EnemyEvent> events;
      auto engage = [&](float dist, bool armed, sim::Side side, float seconds, int& rounds) {
        auto h = hover();
        h->noCrash = false;
        sim::Targets t(terrain);
        int k = t.add(kDshk, h->pos.x + dist * 0.6f, h->pos.z - dist * 0.8f, 0.0f);
        t.list[size_t(k)].armed = armed;
        t.list[size_t(k)].side = side;
        sim::EnemyFire e(terrain);
        rounds = 0;
        for (int i = 0; i < int(seconds / kDt); ++i) {
          events.clear();
          e.step(*h, t, kDt, events);
          for (const auto& ev : events) rounds += ev.type == sim::EnemyEvent::Muzzle;
        }
        return 100.0f - h->damage.airframe + (200.0f - h->damage.engine[0] - h->damage.engine[1]) + (100.0f - h->damage.tailRotor);
      };
      int bursts = 0;
      float hurt = engage(1000.0f, true, sim::Side::Enemy, 60.0f, bursts);
      check(bursts >= 5 && hurt > 2.0f, "enemy: DShK at 1 km fires bursts and hits (damage)", hurt);
      float none = engage(2600.0f, true, sim::Side::Enemy, 30.0f, bursts) + engage(1000.0f, false, sim::Side::Enemy, 30.0f, bursts) +
                   engage(1000.0f, true, sim::Side::Ours, 30.0f, bursts);
      check(none == 0.0f, "enemy: out of reach, unarmed or ours: no fire", none);
      {
        // An enemy fighter on its route 5 km off (a gunship 3 km) sees the hovering Hind, leaves the route and
        // attacks it in passes (gun and missiles); an enemy gunship closes to its gun's range.
        auto fly = [&](sim::Arms arms, sim::UnitClass cls, float seconds, bool& engaged, float& closest) {
          sim::VehicleType jt;
          jt.name = arms == sim::Arms::Fighter ? "TEST FIGHTER" : "TEST GUNSHIP";
          jt.original = jt.name;
          jt.cls = cls;
          jt.armour = sim::Armour::Light;
          jt.health = 10.0f;
          jt.lo = {-5, -1, -8}, jt.hi = {5, 2, 8};
          jt.arms = arms;
          const sim::VehicleKind kind = sim::registerVehicleType(jt);
          auto h = hover();
          h->noCrash = false;
          sim::Targets t(terrain);
          const float off = arms == sim::Arms::Fighter ? 5000.0f : 3000.0f;
          const int j = t.add(kind, h->pos.x + off, h->pos.z + 1000.0f, glm::radians(-90.0f));
          sim::Vehicle& v = t.list[size_t(j)];
          v.side = sim::Side::Enemy;
          v.pos.y = terrain.groundHeight(v.pos.x, v.pos.z) + 200.0f;
          t.setRoute(j, {{{v.pos.x, v.pos.z}, 200.0f, 120.0f}, {{v.pos.x, v.pos.z + 20000.0f}, 200.0f, 120.0f}}, false, 0.0f);
          sim::EnemyFire e(terrain);
          sim::AirCombat air(terrain);
          engaged = false;
          closest = 1e9f;
          for (int i = 0; i < int(seconds / kDt); ++i) {
            events.clear();
            t.step(kDt);
            air.step(t, {h.get()}, kDt);
            e.step(*h, t, kDt, events);
            engaged = engaged || air.attacking(j);
            closest = std::min(closest, glm::distance(t.list[size_t(j)].pos, h->pos));
          }
          return 100.0f - h->damage.airframe + (200.0f - h->damage.engine[0] - h->damage.engine[1]) + (100.0f - h->damage.tailRotor);
        };
        bool engaged = false, engaged2 = false;
        float closest = 0.0f, closest2 = 0.0f;
        const float hurtJet = fly(sim::Arms::Fighter, sim::UnitClass::Aircraft, 120.0f, engaged, closest);
        check(engaged && closest < 1200.0f && hurtJet > 0.0f, "enemy: a fighter leaves its route and attacks the Hind (damage)", hurtJet);
        const float hurtGunship = fly(sim::Arms::Gunship, sim::UnitClass::Helicopter, 150.0f, engaged2, closest2);
        check(engaged2 && closest2 < 1500.0f && closest2 > 500.0f && hurtGunship > 0.0f,
              "enemy: a gunship closes to its gun's range and fires (damage)", hurtGunship);
      }
      {
        // Two Hinds (the network game's wingman): the DShK fires at the nearer, 1 km off; the
        // other, 3 km away, is left alone.
        auto a = hover(), b = hover();
        a->noCrash = b->noCrash = false;
        b->pos.x += 2000.0f;
        sim::Targets t(terrain);
        t.add(kDshk, b->pos.x + 600.0f, b->pos.z - 800.0f, 0.0f);
        sim::EnemyFire e(terrain);
        for (int i = 0; i < int(60.0f / kDt); ++i) {
          events.clear();
          e.step(*a, t, kDt, events, {b.get()});
        }
        auto hurt = [](const sim::Helicopter& h) {
          return 100.0f - h.damage.airframe + (200.0f - h.damage.engine[0] - h.damage.engine[1]) + (100.0f - h.damage.tailRotor);
        };
        const float hurtA = hurt(*a), hurtB = hurt(*b);
        check(hurtA == 0.0f && hurtB > 0.0f, "enemy: of two Hinds the nearer is fired at (its damage)", hurtB);
      }

      // Strela-2 from 1.5 km behind (not every man has a missile ready): without countermeasures
      // about half of them find an engine; the jammer and flares fired after the launch call
      // defeat most.
      int hits[2] = {0, 0}, launched[2] = {0, 0};
      for (int defend = 0; defend < 2; ++defend)
        for (int trial = 0; trial < 60; ++trial) {
          auto h = hover();
          h->noCrash = false;
          sim::Targets t(terrain);
          t.add(kSam7, h->pos.x + 220.0f, h->pos.z + 1500.0f, 0.0f);
          sim::EnemyFire e(terrain);
          e.seed(1000u + unsigned(trial) * 7919u);
          e.jammer = defend;
          bool launch = false;
          for (int i = 0; i < int(20.0f / kDt); ++i) {
            events.clear();
            e.step(*h, t, kDt, events);
            for (const auto& ev : events) launch = launch || ev.type == sim::EnemyEvent::Launch;
            // Flares a second after the launch call.
            if (defend && !e.shots().empty() && e.shots()[0].age > 1.0f && e.flares == sim::EnemyFire::kFlares)
              e.releaseFlares(*h);
          }
          launched[defend] += launch;
          if (h->damage.engine[0] < 100.0f || h->damage.engine[1] < 100.0f) ++hits[defend];
        }
      const float pk = float(hits[0]) / float(std::max(launched[0], 1)), pkDefended = float(hits[1]) / float(std::max(launched[1], 1));
      check(launched[0] > 12 && launched[0] < 40, "enemy: about 40 % of the men have a Strela ready (of 60)", launched[0]);
      check(pk > 0.3f && pk < 0.8f, "enemy: Strela-2 finds an engine (hits per launch)", pk);
      check(pkDefended < 0.3f && pkDefended < pk, "enemy: jammer and flares defeat most (hits per launch)", pkDefended);
      {
        // The flare launcher left on fires a pair every second or so and runs dry; the chaff stays.
        auto h = hover();
        sim::Targets t(terrain);
        sim::EnemyFire e(terrain);
        e.autoFlares = true;
        for (int i = 0; i < int(30.0f / kDt); ++i) e.step(*h, t, kDt, events);
        const int used30 = sim::EnemyFire::kFlares - e.flares;
        for (int i = 0; i < int(120.0f / kDt); ++i) e.step(*h, t, kDt, events);
        check(used30 > 30 && used30 < 110 && e.flares == 0 && e.chaff == sim::EnemyFire::kChaff,
              "countermeasures: auto flares, ~2/s, run dry", used30);
      }
    }
    {
      // Battle damage: a failed engine leaves ~60 % torque, a lost tail rotor spins the hovering
      // helicopter, a destroyed airframe brings it down.
      auto h = hover();
      h->noCrash = false;
      h->hit(Helicopter::HitPart::Engine1, 150.0f);
      sim::Controls c;
      c.collective = 1.3f;
      run(*h, c, 1.0f);
      check(h->engineOut(0) && h->collective < 0.79f && h->message == "ENGINE 1 FAILED", "damage: one engine, torque limited to 78 %",
            h->collective);
      auto g = hover();
      g->noCrash = false;
      g->hit(Helicopter::HitPart::TailRotor, 150.0f);
      sim::Controls hc;
      hc.collective = Helicopter::levelFlightCollective(0.0f, g->mass);
      run(*g, hc, 4.0f);
      check(std::fabs(glm::degrees(g->omega.y)) > 30.0f, "damage: tail rotor lost, it spins (deg/s)", glm::degrees(g->omega.y));
      auto f = hover();
      f->noCrash = false;
      f->hit(Helicopter::HitPart::Airframe, 120.0f);
      run(*f, hc, 30.0f);
      check(f->damage.shotDown && f->crashed, "damage: airframe destroyed, shot down and crashed", f->damage.airframe);
    }

    // Ground targets.
    {
      sim::Targets t(terrain);
      const glm::vec2 c = af.center;
      int brdm = t.add(sim::VehicleKind::Brdm2, c.x, c.y - 300.0f, glm::radians(90.0f));
      const sim::Vehicle& v = t.list[size_t(brdm)];
      glm::vec3 hit;
      // Broadside (it faces east): 2.3 m wide along z, 5.9 m long along x.
      check(t.raycast(v.pos + glm::vec3(-10, 1, 2.5f), v.pos + glm::vec3(10, 1, 2.5f), hit) < 0 &&
                t.raycast(v.pos + glm::vec3(2.5f, 1, -10), v.pos + glm::vec3(2.5f, 1, 10), hit) == brdm,
            "targets: hit box turns with the vehicle", 0.0);
      check(t.raycast(v.pos + glm::vec3(0, 1, 50), v.pos + glm::vec3(0, 1, -50), hit) == brdm &&
                std::fabs(hit.z - v.pos.z - 1.15f) < 0.01f,
            "targets: ray stops at the near side", hit.z - v.pos.z);
      check(t.raycast(v.pos + glm::vec3(0, 2.5f, 50), v.pos + glm::vec3(0, 2.5f, -50), hit) < 0,
            "targets: ray over the roof misses", 0.0);
      check(std::fabs(v.pos.y - terrain.groundHeight(v.pos.x, v.pos.z)) < 0.3f, "targets: stands on the ground",
            v.pos.y - terrain.groundHeight(v.pos.x, v.pos.z));
    }
    {
      // The gunner holds the turret on a BRDM-2 300 m ahead: a 3 s burst destroys it.
      auto h = hover();
      sim::Targets t(terrain);
      int brdm = t.add(sim::VehicleKind::Brdm2, h->pos.x, h->pos.z - 300.0f, glm::radians(90.0f));
      int tank = t.add(sim::VehicleKind::T62, h->pos.x + 40.0f, h->pos.z - 300.0f, glm::radians(90.0f));
      sim::Weapons w(terrain);
      w.load({});
      w.selectNext();
      auto aimAt = [&](glm::vec3 p) {
        glm::vec3 d = glm::inverse(h->orient) * glm::normalize(p - (h->pos + sim::Weapons::kTurretPivot));
        w.aimTurret(std::atan2(-d.x, -d.z), std::asin(d.y));
      };
      aimAt(t.list[size_t(brdm)].pos + glm::vec3(0, 1.6f, 0));
      impacts.clear();
      for (int i = 0; i < int(3.0f / kDt); ++i) w.step(*h, i * kDt > 1.0f, kDt, impacts, &t);
      for (int i = 0; i < int(1.0f / kDt); ++i) w.step(*h, false, kDt, impacts, &t);
      int onTarget = int(std::count_if(impacts.begin(), impacts.end(), [&](const sim::Impact& i) { return i.target == brdm; }));
      bool blown = std::any_of(impacts.begin(), impacts.end(), [&](const sim::Impact& i) { return i.destroyed; });
      check(t.list[size_t(brdm)].destroyed && blown && h->message == "BRDM-2 DESTROYED",
            "targets: Yak-B burst destroys a BRDM-2 at 300 m (hits)", onTarget);
      check(t.alive() == 1, "targets: one left alive", t.alive());
      // 12.7 mm bullets do nothing to a tank.
      aimAt(t.list[size_t(tank)].pos + glm::vec3(0, 1.4f, 0));
      for (int i = 0; i < int(1.0f / kDt); ++i) w.step(*h, false, kDt, impacts, &t);
      impacts.clear();
      for (int i = 0; i < int(2.0f / kDt); ++i) w.step(*h, true, kDt, impacts, &t);
      for (int i = 0; i < int(1.0f / kDt); ++i) w.step(*h, false, kDt, impacts, &t);
      onTarget = int(std::count_if(impacts.begin(), impacts.end(), [&](const sim::Impact& i) { return i.target == tank; }));
      check(onTarget > 20 && t.list[size_t(tank)].health == sim::vehicleType(sim::VehicleKind::T62).health,
            "targets: T-62 shrugs off Yak-B hits", onTarget);
    }
    if (const sim::VehicleKind hind = sim::findVehicleType("Mi_24_Hind_Wingman"); hind != sim::VehicleKind::Count) {
      // A Hind waiting up in the air to start its route holds there (drawn flying, its rotor turning).
      sim::Targets t(terrain);
      const int up = t.add(hind, 0.0f, 0.0f, 0.0f), low = t.add(hind, 50.0f, 0.0f, 0.0f);
      t.setRoute(up, {{{0.0f, 0.0f}, 30.0f, 40.0f}, {{0.0f, -500.0f}, 30.0f, 40.0f}}, false, 20.0f);
      t.setRoute(low, {{{50.0f, 0.0f}, 30.0f, 0.0f}, {{50.0f, -500.0f}, 30.0f, 40.0f}}, false, 20.0f);
      check(t.list[size_t(up)].hover && !t.list[size_t(low)].hover, "targets: a Hind waiting aloft holds in the air, one on the ground stands", 0);
    }
    {
      // A Hind of the network game in the line of fire: our rounds hit it (the pilot's game then
      // counts them), and a door gunner firing by himself holds his fire through it.
      auto h = hover(), other = hover();
      other->noCrash = false;
      other->pos = h->pos + glm::vec3(0.0f, -1.0f, -150.0f);
      sim::Targets t(terrain);
      sim::Weapons w(terrain);
      w.load({});
      w.selectNext();
      w.otherHinds.push_back({other.get(), int8_t(1)});
      std::vector<sim::Impact> imp;
      for (int i = 0; i < int(2.0f / kDt); ++i) w.step(*h, false, kDt, imp, &t);
      for (int i = 0; i < int(2.0f / kDt); ++i) w.step(*h, true, kDt, imp, &t);
      const auto struck = std::count_if(imp.begin(), imp.end(), [](const sim::Impact& i) { return i.hind == 1; });
      check(struck > 5, "network: our rounds hit another Hind ahead", float(struck));
      const float before = other->damage.airframe;
      for (const sim::Impact& i : imp)
        if (i.hind == 1) sim::roundOnHind(*other, i.type, i.pos, "IVANOV");
      check(other->damage.airframe < before && other->damage.lastHitBy == "IVANOV", "network: a round on another Hind damages it, its shooter named", other->damage.airframe);
      auto far = hover();
      far->noCrash = false;
      check(sim::blastOnHind(*far, sim::WeaponType::Fab250, far->pos + glm::vec3(5.0f, 0.0f, 0.0f), "IVANOV") && far->damage.airframe < 100.0f &&
                !sim::blastOnHind(*far, sim::WeaponType::YakB, far->pos, "IVANOV"),
            "network: a bomb's blast hurts another Hind, a bullet has none", far->damage.airframe);
    }
    {
      // Arcade's door gun: the other weapons endless, it runs dry, reloads for a while, full again.
      auto h = hover();
      sim::Targets t(terrain);
      sim::Weapons w(terrain);
      sim::Loadout lo;
      lo.doorGun[1] = true;
      w.infiniteAmmo = true, w.doorReloads = true;
      w.load(lo);
      std::vector<sim::Impact> imp;
      w.setDoorGunner(1, glm::vec3(1.0f, 0.0f, 0.0f), true);
      float sec = 0.0f;
      while (w.doorRounds(1) > 0 && sec < 60.0f) w.step(*h, false, kDt, imp, &t), sec += kDt;
      const bool ran = w.doorRounds(1) == 0 && sec > 10.0f;
      w.step(*h, false, kDt, imp, &t);
      const bool loading = w.doorReloading(1) && h->message == "RELOADING GSG";
      imp.clear();
      w.shots.clear();
      for (float s = 0.0f; s < sim::Weapons::kDoorReload - 0.2f; s += kDt) w.step(*h, false, kDt, imp, &t);
      const bool silent = w.shots.empty() && w.doorRounds(1) == 0;
      for (float s = 0.0f; s < 0.3f; s += kDt) w.step(*h, false, kDt, imp, &t);
      check(ran && loading && silent && w.doorRounds(1) > sim::kDoorGunRounds - 40 && !w.doorReloading(1), "arcade: the door gun runs dry, reloads, full again (s of fire)", sec);
      check(w.reloads() == 1, "arcade: and the voice says weapons reloaded", w.reloads());
    }
    {
      // Arcade's debriefing: the score and the kills, no mission's targets; down is failure.
      std::map<std::string, sim::ArcadeKill> kills;
      kills["GERBERA DECOY"] = {2, 0}, kills["SHAHED-136"] = {7, 1050};
      const std::vector<std::string> d = sim::arcadeDebrief(true, "Hind crashed.", true, 1050, 0, 125.0, kills);
      const auto has = [&](const char* line) { return std::find(d.begin(), d.end(), line) != d.end(); };
      check(has("Score: 1050") && has("Time in the air: 2:05") && has("Hind crashed.") && has("Destroyed by you: 9") && has("  7 x SHAHED-136  (1050)") &&
                sim::arcadeResult(true, 9000) == sim::MissionResult::Failed && sim::arcadeResult(false, 100) == sim::MissionResult::Success &&
                sim::arcadeResult(false, 3000) == sim::MissionResult::Outstanding,
            "arcade: debriefing by score and kills", int(d.size()));
    }
    {
      // The turret gun at a Shahed standing in the air ahead: the rounds that got there before it
      // came down, on average (those still in the air not counted), more the further off.
      const auto roundsAt = [&](float dist) {
        int total = 0;
        sim::Weapons w(terrain);
        for (int trial = 0; trial < 12; ++trial) {
          auto h = hover();
          sim::Targets t(terrain);
          const int d = t.add(sim::VehicleKind::Shahed, h->pos.x, h->pos.z - dist, glm::radians(90.0f));
          t.list[size_t(d)].pos.y = h->pos.y - 20.0f;
          w.load({});
          w.selectNext();
          const glm::vec3 muzzle = h->pos + h->orient * sim::Weapons::kTurretPivot;
          const glm::vec3 aim = glm::inverse(h->orient) * sim::Weapons::gunAim(muzzle, h->vel, t.list[size_t(d)].pos, glm::vec3(0.0f));
          w.aimTurret(std::atan2(-aim.x, -aim.z), std::asin(aim.y));
          std::vector<sim::Impact> imp;
          for (int i = 0; i < int(2.0f / kDt); ++i) w.step(*h, false, kDt, imp, &t);
          const int before = w.rounds(sim::WeaponType::YakB);
          for (int i = 0; i < int(40.0f / kDt) && !t.list[size_t(d)].destroyed && w.rounds(sim::WeaponType::YakB) > 0; ++i) w.step(*h, true, kDt, imp, &t);
          total += before - w.rounds(sim::WeaponType::YakB);
          for (const sim::Projectile& s : w.shots) total -= glm::distance(s.pos, muzzle) < dist;
        }
        return float(total) / 12.0f;
      };
      const float at300 = roundsAt(300.0f), at1000 = roundsAt(1000.0f), at2000 = roundsAt(2000.0f);
      std::printf("Yak-B rounds per Shahed: %.0f at 300 m, %.0f at 1000 m, %.0f at 2000 m\n", at300, at1000, at2000);
      check(at300 >= 15.0f && at300 < 60.0f, "drones: Yak-B on a Shahed at 300 m (rounds)", at300);
      check(at1000 > 2.0f * at300 && at2000 > 2.0f * at1000, "drones: Yak-B needs more rounds the further off (at 1000 m)", at1000);
      check(at2000 > 400.0f, "drones: Yak-B on a Shahed at 2 km (rounds)", at2000);
    }
    {
      // S-8 blast: a near miss wrecks a fuel truck, a tank only gets scratched.
      sim::Targets t(terrain);
      const glm::vec2 c = af.center;
      int truck = t.add(sim::VehicleKind::FuelTruck, c.x, c.y, 0.0f);
      int tank = t.add(sim::VehicleKind::T62, c.x + 12.0f, c.y, 0.0f);
      glm::vec3 burst = t.list[size_t(truck)].pos + glm::vec3(5.0f, 0.0f, 0.0f);  // 3.75 m from the truck's side
      std::vector<int> killed = t.damage(-1, sim::WeaponType::S8, burst);
      const sim::Vehicle& k = t.list[size_t(tank)];
      check(killed.size() == 1 && killed[0] == truck && !k.destroyed && k.health > 25.0f,
            "targets: S-8 near miss destroys a truck, not a tank", k.health);
      // The shaped charge of one direct hit finishes a BMP-2; a tank takes three.
      int bmp = t.add(sim::VehicleKind::Bmp2, c.x - 40.0f, c.y, 0.0f);
      t.damage(bmp, sim::WeaponType::S8, t.list[size_t(bmp)].pos);
      int hits = 0;
      while (!t.list[size_t(tank)].destroyed && hits < 10) t.damage(tank, sim::WeaponType::S8, t.list[size_t(tank)].pos), ++hits;
      check(t.list[size_t(bmp)].destroyed && hits == 3, "targets: S-8 hits: one destroys a BMP-2, three a T-62", hits);
    }
    {
      sim::Targets t(terrain);
      t.placeExercise();
      bool dry = std::none_of(t.list.begin(), t.list.end(), [&](const sim::Vehicle& v) { return terrain.isWater(v.pos.x, v.pos.z); });
      check(t.list.size() == 12 && dry, "targets: exercise area has 12 vehicles on dry land", double(t.list.size()));
    }
  }

  // Fuel and rpm: half power on the ground for a minute, then the tanks run dry.
  {
    Helicopter h(terrain);
    h.resetOnGround(af.center, 0.0f);
    sim::Controls c;
    c.collective = 0.5f;
    run(h, c, 60.0f);
    float burnt = Helicopter::kFuelFull - h.fuel;
    check(burnt > 6.0f && burnt < 10.0f && std::fabs(h.rotorRpm - 100.0f) < 1.0f && h.engineRpm > 85.0f,
          "engines: a minute at half power burns 8 kg, rotor at 100 %", burnt);
    h.fuel = 0.01f;
    run(h, c, 10.0f);
    check(h.fuel == 0.0f && h.torque == 0.0f && h.rotorFree && h.engineRpm < 10.0f, "engines: out of fuel they stop, the rotor turns free",
          h.engineRpm);
  }

  // Threat warning: radars that see the helicopter.
  {
    auto h = std::make_unique<Helicopter>(terrain);
    glm::vec3 start{af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 150.0f, af.center.y};
    h->resetAirborne(start, 0.0f, 0.0f);
    sim::Targets t(terrain);
    t.add(sim::VehicleKind::T62, start.x, start.z - 1000.0f, 0.0f);
    check(!sim::greatestThreat(*h, t, terrain).active, "rwr: a tank has no radar", 0.0);
    int zsu = t.add(sim::VehicleKind::Zsu23, start.x + 2000.0f, start.z, 0.0f);
    sim::Threat th = sim::greatestThreat(*h, t, terrain);
    check(th.active && th.cls == sim::ThreatClass::AaaRadar && std::fabs(th.bearingDeg - 90.0f) < 2.0f &&
              std::fabs(th.proximity - 0.75f) < 0.02f,
          "rwr: ZSU-23-4 2 km to the right, AAA radar at 3 o'clock", th.bearingDeg);
    t.list[size_t(zsu)].destroyed = true;
    t.add(sim::VehicleKind::Sa13, start.x, start.z - 12000.0f, 0.0f);
    check(!sim::greatestThreat(*h, t, terrain).active, "rwr: wrecks and radars out of range stay dark", 0.0);
  }

  // Silicon WSO.
  {
    auto h = std::make_unique<Helicopter>(terrain);
    glm::vec3 start{af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 60.0f, af.center.y};
    h->resetAirborne(start, 0.0f, 0.0f);
    h->noCrash = true;
    sim::Targets t(terrain);
    const glm::vec2 c{start.x, start.z};
    int near = t.add(sim::VehicleKind::Brdm2, c.x + 30.0f, c.y - 400.0f, 0.0f);
    int far = t.add(sim::VehicleKind::Bmp2, c.x - 150.0f, c.y - 700.0f, 0.0f);
    t.add(sim::VehicleKind::T62, c.x, c.y + 300.0f, 0.0f);  // behind: he does not look there
    sim::Weapons w(terrain);
    w.load({});
    w.selectNext();
    sim::Wso wso(terrain);
    std::vector<sim::Impact> impacts;
    wso.update(*h, w, t, kDt);
    check(wso.target() == near && h->message == "WSO: BRDM-2", "wso: picks the nearest target ahead", wso.target());
    wso.rescan(*h, t);
    int second = wso.target();
    wso.rescan(*h, t);
    check(second == far && wso.target() == near, "wso: B cycles through the targets ahead only", second);
    glm::vec3 eye = h->pos;
    wso.track(*h, t, eye, glm::normalize(t.list[size_t(far)].pos + glm::vec3(0, 1, 0) - eye));
    check(wso.target() == far, "wso: T tracks the target in the line of sight", wso.target());
    {
      // Our own vehicle ahead: not a target, unless he is told to take allies too.
      sim::Targets ours(terrain);
      const int friendly = ours.add(sim::VehicleKind::Bmp2, c.x, c.y - 500.0f, 0.0f);
      ours.list[size_t(friendly)].side = sim::Side::Ours;
      sim::Wso a(terrain), b(terrain);
      b.allies = true;
      a.rescan(*h, ours);
      b.rescan(*h, ours);
      check(a.target() == -1 && b.target() == friendly, "wso: our own forces targets only with 'WSO targets allies'", b.target());
    }
    // He lays the gun with lead; the pilot only holds the trigger.
    float sec = 0;
    for (; sec < 10.0f && !t.list[size_t(far)].destroyed; sec += kDt) {
      wso.update(*h, w, t, kDt);
      w.step(*h, sec > 1.0f, kDt, impacts, &t);
    }
    check(t.list[size_t(far)].destroyed, "wso: aims the Yak-B onto a BMP-2 at 720 m (s)", sec - 1.0);
    for (float k = 0; k < 3.0f; k += kDt) wso.update(*h, w, t, kDt);
    check(wso.target() == near, "wso: moves on to the next target", wso.target());
    wso.setEnabled(false, *h);
    wso.reset();
    for (float k = 0; k < 3.0f; k += kDt) wso.update(*h, w, t, kDt);
    check(wso.target() < 0 && h->message == "SILICON WSO OFF", "wso: switched off he does not search", wso.target());
  }
  {
    // The WSO's Yak-B against men on foot 1200 m ahead of a hovering helicopter: the round's
    // path with drag (its fall slows too) puts most of the gun's 1470 rounds among them.
    sim::VehicleType man;
    man.name = "SOLDIER";
    man.cls = sim::UnitClass::Soldier;
    man.armour = sim::Armour::Soft;
    man.health = 1.0f;
    man.lo = {-0.3f, 0.0f, -0.3f};
    man.hi = {0.3f, 1.8f, 0.3f};
    const sim::VehicleKind kMan = sim::registerVehicleType(man);
    Helicopter h(terrain);
    h.resetAirborne({af.center.x, terrain.groundHeight(af.center.x, af.center.y) + 80.0f, af.center.y}, 0.0f, 0.0f);
    h.noCrash = true;
    sim::Targets t(terrain);
    for (int i = 0; i < 10; ++i) t.add(kMan, af.center.x - 40.0f + 9.0f * float(i), af.center.y - 1200.0f - 7.0f * float(i % 3), 0.0f);
    sim::Weapons w(terrain);
    w.load({});
    w.selectNext();
    sim::Wso wso(terrain);
    std::vector<sim::Impact> imp;
    for (int i = 0; i < int(40.0f / kDt) && w.rounds(sim::WeaponType::YakB) > 0; ++i) {
      wso.update(h, w, t, kDt);
      w.step(h, wso.target() >= 0 && i > int(2.0f / kDt), kDt, imp, &t);
    }
    int dead = 0;
    for (const auto& v : t.list) dead += v.destroyed;
    check(dead >= 6, "wso: Yak-B kills men on foot at 1200 m (of 10)", dead);
  }

  // Models of the original game, when it is installed here.
  {
    gamedata::OriginalData original;
    std::string dir = gamedata::findOriginalDir("");
    if (dir.empty() || !original.open(dir)) {
      std::printf("[SKIP] original game data not found\n");
    } else {
      {
        // The lone bowman among the zone's objects, by their real shape: along a low bridge (up
        // onto it by a step), and past its piers underneath a high one.
        const world::Terrain ground;
        const float base = ground.groundHeight(0.0f, 0.0f);
        const auto shapeOf = [&](const char* name) {
          const sim::VehicleType& t = sim::vehicleType(sim::unitKind(original, original.find(name)));
          return sim::UnitShape{glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -base, 0.0f)), t.lo, t.hi, &t.faces};
        };
        // Walks along z through the object at x = 0: how high he gets and whether something stops him.
        const auto along = [&](const sim::UnitShape& u, float& top) {
          float feet = base;
          top = 0.0f;
          for (float z = u.hi.z + 4.0f; z > u.lo.z - 4.0f; z -= 0.1f) {
            const float next = sim::Bowman::floorAt({0.0f, z}, feet, ground, {u});
            if (sim::Bowman::blocked({0.0f, next, z}, {}, {u})) return false;
            feet = next, top = std::max(top, feet - base);
          }
          return true;
        };
        float top = 0.0f;
        { const bool through = along(shapeOf("Bridge_Road_Piled"), top); check(through && top > 2.5f, "bowman: walks over a low road bridge (m up)", top); }
        { const bool through = along(shapeOf("Bridge_Road_30_Hump"), top); check(through && top > 2.5f, "bowman: walks over a humped bridge (m up)", top); }
        { const bool through = along(shapeOf("Bridge_Road30_over_Mway"), top); check(through && top < 0.5f, "bowman: walks under a high bridge, between its piers (m up)", top); }
        check(!along(shapeOf("Afghan_House_5"), top), "bowman: a house's wall stops him", top);
        // Standing inside a shape (a man walked into him) he may leave it, and something else still stops him.
        sim::Bowman man;
        man.place({0.0f, 0.0f}, 0.0f, ground);
        const sim::UnitShape around{glm::translate(glm::mat4(1.0f), -man.pos), {-0.3f, 0.0f, -0.3f}, {0.3f, 1.8f, 0.3f}};
        const sim::UnitShape wall{glm::translate(glm::mat4(1.0f), -man.pos), {-3.0f, 0.0f, -3.0f}, {3.0f, 3.0f, -2.0f}};
        for (int k = 0; k < 300; ++k) man.walk({0.0f, 1.0f}, false, 0.02f, ground, {}, {around, wall});
        check(man.pos.z < -0.5f && man.pos.z > -2.0f, "bowman: out of a man he stands in, then stopped by a wall (m)", man.pos.z);
        {
          // A jump some 0.75 m high and back on the ground, nothing hurting; off a road bridge over
          // its railing he falls to the ground, and that hurts for a second.
          sim::Bowman j;
          j.place({0.0f, 0.0f}, 0.0f, ground);
          const float stand = j.pos.y;
          float peak = 0.0f;
          j.jump();
          for (int k = 0; k < 120; ++k) j.fall(1.0f / 60.0f, ground, {}), peak = std::max(peak, j.pos.y - stand);
          check(peak > 0.6f && peak < 0.9f && !j.airborne && j.pain == 0.0f && std::fabs(j.pos.y - stand) < 0.01f, "bowman: jumps up and comes down (m high)", peak);
          const sim::UnitShape deck = shapeOf("Bridge_Road_Piled");
          j.place({0.0f, 40.0f}, 0.0f, ground);
          j.pos.y = base;
          // Up onto the bridge from its end, to its middle, then sideways over the railing.
          for (int k = 0; k < 400 && j.pos.z > 0.0f; ++k) j.walk({0.0f, 1.0f}, false, 1.0f / 60.0f, ground, {}, {deck}), j.fall(1.0f / 60.0f, ground, {deck});
          const float onDeck = j.pos.y - base;
          j.vel = glm::vec2(0.0f);  // (stopped there, not carried on past the middle)
          for (int k = 0; k < 40; ++k) j.walk({1.0f, 0.0f}, false, 1.0f / 60.0f, ground, {}, {deck}), j.fall(1.0f / 60.0f, ground, {deck});
          const float atRail = j.pos.x;
          bool flew = false;
          float hurt = 0.0f;
          j.jump();
          for (int k = 0; k < 180; ++k) {
            j.walk({1.0f, 0.0f}, false, 1.0f / 60.0f, ground, {}, {deck});
            j.fall(1.0f / 60.0f, ground, {deck});
            hurt = std::max(hurt, j.pain);
            flew = flew || j.pos.x > 5.5f;
          }
          std::printf("bowman on the deck %.2f m up, at the railing x %.2f, then x %.2f y %.2f\n", onDeck, atRail, j.pos.x, j.pos.y - base);
          check(onDeck > 2.5f && flew, "bowman: over a bridge's railing with a jump (m out)", j.pos.x);
          check(hurt > 0.5f && j.pain < 0.01f && !j.airborne && j.pos.y - base < 0.1f, "bowman: the fall off it hurts, gone within a second", hurt);
        }
      }
      for (int k = 0; k < int(sim::VehicleKind::Count); ++k) {
        const sim::VehicleType& vt = sim::vehicleType(sim::VehicleKind(k));
        if (vt.cls == sim::UnitClass::Drone) continue;  // ours, not in the original
        gamedata::Model m = original.model(original.find(vt.original));
        // The hull fits the hit box; guns and aerials may stick out.
        float dx = std::max(std::fabs(m.lo.x - vt.lo.x), std::fabs(m.hi.x - vt.hi.x));
        float dz = std::fabs(m.hi.z - vt.hi.z);
        char what[80];
        std::snprintf(what, sizeof what, "original: %s model decoded, fits its hit box", vt.name.c_str());
        check(m.polys.size() > 50 && original.skipped() == 0 && dx < 0.6f && dz < 0.4f && m.lo.y > -0.3f, what,
              std::max(dx, dz));
      }
      for (const char* name : {"COCKPITS/PILOT.LBM", "COCKPITS/GUNNER.LBM"}) {
        gamedata::Picture pic;
        std::string error;
        bool ok = gamedata::loadPbm(gamedata::findOriginalFile(dir, name), pic, 0, error);
        size_t clear = 0;
        for (size_t i = 3; i < pic.rgba.size(); i += 4) clear += pic.rgba[i] == 0;
        double share = pic.rgba.empty() ? 0.0 : double(clear) / double(pic.rgba.size() / 4);
        char what[80];
        std::snprintf(what, sizeof what, "original: %s 1920x480 panorama, windows clear", name);
        check(ok && pic.width == 1920 && pic.height == 480 && share > 0.4 && share < 0.8, what, share);
      }
      // The war zone of Afghanistan: its airfields where they are on the map, heights in feet.
      gamedata::Zone zone;
      std::string zoneError;
      bool zoneOk = gamedata::loadZone(dir, "AFGHAN.WZ", zone, zoneError);
      const gamedata::ZoneAirfield* bagram = zoneOk ? zone.airfield("BAGRAM") : nullptr;
      {
        // Kazakhstan and Saratov: a copy of Korea's airfield list; their own found by the runway
        // markings, each of them with a runway laid out there.
        for (const char* z : {"KAZAK.WZ", "SARATOV.WZ"}) {
          gamedata::Zone kz;
          std::string e;
          std::vector<gamedata::ZoneAirfield> fields;
          if (original.loaded() && gamedata::loadZone(dir, z, kz, e)) fields = gamedata::airfieldsFromMarkings(kz, original);
          const size_t runways = fields.empty() ? 0 : gamedata::zoneRunways(kz, original, fields[0].pos).size();
          check(!fields.empty() && runways >= 1, (std::string("original: ") + z + " airfields by their runway markings (runways at the first)").c_str(),
                double(runways));
        }
      }
      const gamedata::ZoneAirfield* kabul = zoneOk ? zone.airfield("KABUL") : nullptr;
      float apart = bagram && kabul ? glm::length(bagram->pos - kabul->pos) / 1000.0f : 0.0f;
      check(zoneOk && zone.size == 257 && apart > 40.0f && apart < 50.0f,
            "original: Afghanistan, Bagram 40-50 km north of Kabul (km)", apart);
      if (bagram) {
        float top = 0.0f;
        for (float h : zone.height) top = std::max(top, h);
        world::Terrain afghan(zone.heightMap(bagram->pos, 300.0f));
        check(top > 5000.0f && top < 7000.0f && std::fabs(afghan.groundHeight(0.0f, 0.0f) - 300.0f) < 20.0f &&
                  !afghan.isWater(0.0f, 0.0f),
              "original: Afghan peaks 5-7 km over the valleys, Bagram in a dry valley (m)", top);
      }
      // Missions: the formations from HIND.EXE, the first Afghan campaign mission's forces.
      std::vector<gamedata::Formation> formations;
      bool formOk = gamedata::loadFormations(dir, formations, zoneError);
      bool dshk = formOk && formations.size() == 142 && formations[134].members.size() == 2 &&
                  original.name(formations[134].members[0].object) == "DShK_HMG_Tripod";
      check(dshk, "mission: 141 formations, 134 = DShK and its gunner", double(formations.size()));
      gamedata::Mission mission;
      bool missionOk = gamedata::loadMission(dir, "afghnc01", mission, zoneError);
      const gamedata::ZoneAirfield* jalalabad = zoneOk ? zone.airfield("JALLALABAD") : nullptr;
      float fromBase = missionOk && jalalabad ? glm::distance(mission.player()->route[0].pos, jalalabad->pos) : 1e9f;
      check(missionOk && mission.zone == 3 && mission.title == "Support Allied Counter-Attacks" &&
                mission.fact("Targets") == "Heavy Machine Gun Sites" && mission.successCount == 20 && fromBase < 1500.0f,
            "mission: AFGHNC01 briefing, starts at Jalalabad (m)", fromBase);
      if (missionOk && jalalabad && formOk) {
        world::Terrain land(zone.heightMap(jalalabad->pos, 300.0f));
        sim::Targets units(land);
        sim::MissionState ms;
        ms.start(mission, formations, original, jalalabad->pos, units);
        int ours = 0, moving = 0;
        for (const sim::Vehicle& v : units.list) ours += v.side == sim::Side::Ours, moving += v.moving();
        check(ms.successTotal() == 30 && ours > 50 && moving > 100 && ms.route().size() == 7,
              "mission: 30 primary targets (15 DShK sites), our troops, moving units", ms.successTotal());
        // A truck on the move gets along its route.
        int truck = -1;
        for (size_t k = 0; k < units.list.size() && truck < 0; ++k)
          if (units.list[k].moving() && sim::vehicleType(units.list[k].kind).name == "TRUCK") truck = int(k);
        glm::vec3 from = truck >= 0 ? units.list[size_t(truck)].pos : glm::vec3(0.0f);
        for (int k = 0; k < 60 * 60; ++k) units.step(1.0f / 60.0f);
        float went = truck >= 0 ? glm::distance(from, units.list[size_t(truck)].pos) : 0.0f;
        check(went > 100.0f, "mission: a truck drives along its route in a minute (m)", went);
        // Steering: passing waypoint B steers to C.
        auto h = std::make_unique<Helicopter>(land);
        glm::vec2 b = ms.route()[1];
        h->resetAirborne({b.x, land.groundHeight(b.x, b.y) + 50.0f, b.y}, 0.0f, 30.0f);
        ms.step(*h, units, 0.1f);
        check(ms.waypoint() == 2, "mission: passing waypoint B steers to C", ms.waypoint());
        check(ms.result() == sim::MissionResult::Flying, "mission: in progress before landing", int(ms.result()));
        // 20 of the guns and their crews destroyed, landed back home: success.
        int killed = 0, lost = -1;
        for (size_t k = 0; k < units.list.size(); ++k) {
          if (units.list[k].objective == sim::Objective::Success && killed < 20) units.destroy(int(k), sim::Killer::Player), ++killed;
          if (lost < 0 && units.list[k].side == sim::Side::Ours && units.list[k].objective == sim::Objective::None) lost = int(k);
        }
        if (lost >= 0) units.destroy(lost, sim::Killer::Enemy);
        ms.step(*h, units, 61.0f);
        glm::vec2 home = ms.route()[0];
        h->resetOnGround(home, 0.0f);
        ms.step(*h, units, 0.1f);
        check(ms.finished() && ms.result() == sim::MissionResult::Success && ms.successKills() == 20,
              "mission: 20 primary kills and back home = success", ms.successKills());
        // The debriefing in the original's words, what was destroyed by whom.
        const std::vector<std::string> deb = ms.debriefing(units);
        auto has = [&](const char* line) { return std::find(deb.begin(), deb.end(), line) != deb.end(); };
        check(has("Good flying.") && has("Enough targets for success.") && has("Targets destroyed by you:") && has("  Enemy Targets:") &&
                  has("Targets destroyed by enemy forces:") && has("  Allied Targets:") && !has("Targets destroyed by other allied forces:"),
              "mission: debriefing as the original's, kills by whom", double(deb.size()));
      }
      gamedata::Mission recon;
      if (gamedata::loadMission(dir, "saratt02", recon, zoneError) && jalalabad && formOk) {
        // Reconnaissance (R): over waypoint X the data is good, elsewhere bad; both X and Y are
        // needed for success.
        world::Terrain land(zone.heightMap(jalalabad->pos, 300.0f));
        sim::Targets units(land);
        sim::MissionState ms;
        ms.start(recon, formations, original, jalalabad->pos, units);
        auto h = std::make_unique<Helicopter>(land);
        auto hover = [&](glm::vec2 p) { h->resetAirborne({p.x, land.groundHeight(p.x, p.y) + 50.0f, p.y}, 0.0f, 0.0f); };
        hover(ms.route()[1]);
        ms.startRecon(*h);
        for (int k = 0; k < 50; ++k) ms.step(*h, units, 0.1f);
        const int bad = ms.reconReport();
        hover(ms.route()[3]);
        ms.startRecon(*h);
        for (int k = 0; k < 50; ++k) ms.step(*h, units, 0.1f);
        const int good = ms.reconReport();
        check(ms.reconTotal() == 2 && bad == -1 && good == 1 && ms.reconDone() == 1,
              "mission: recon over X good, over a turning point bad", ms.reconDone());
        h->resetOnGround(ms.route()[0], 0.0f);
        ms.step(*h, units, 61.0f);
        check(ms.finished() && ms.result() == sim::MissionResult::Failed, "mission: home with only X reported = failed",
              int(ms.result()));
        // Our Hind down in a network game: the mission waits while wingmen still fly.
        sim::MissionState down;
        down.start(recon, formations, original, jalalabad->pos, units);
        hover(down.route()[1]);
        h->crashed = true;
        down.othersAloft(true);
        for (int k = 0; k < 100; ++k) down.step(*h, units, 0.1f);
        const bool waited = !down.finished();
        down.othersAloft(false);
        down.step(*h, units, 0.1f);
        check(waited && down.finished() && down.result() == sim::MissionResult::Failed,
              "mission: our Hind down, it goes on while wingmen fly, ends when they are not", int(waited));
      }
      gamedata::Mission laser;
      if (gamedata::loadMission(dir, "saratt15", laser, zoneError) && jalalabad && formOk) {
        // Remote bombing: the MiG-27 asks for the laser, sees the target at X lit, lets its bombs
        // go; they come down where the laser still holds.
        world::Terrain land(zone.heightMap(jalalabad->pos, 300.0f));
        sim::Targets units(land);
        sim::MissionState ms;
        ms.start(laser, formations, original, jalalabad->pos, units);
        auto h = std::make_unique<Helicopter>(land);
        const glm::vec2 x = ms.route()[3];
        h->resetAirborne({x.x, land.groundHeight(x.x, x.y) + 50.0f, x.y + 1000.0f}, 0.0f, 0.0f);
        std::vector<int> lines;
        std::vector<glm::vec3> bombs;
        for (int k = 0; k < 900 * 10; ++k) {
          ms.laser(true, glm::vec3(x.x + 30.0f, 0.0f, x.y));
          ms.step(*h, units, 0.1f);
          for (int l : ms.takeRadio()) lines.push_back(l);
          for (glm::vec3 b : ms.takeBombs()) bombs.push_back(b);
        }
        const std::vector<int> want{75, 76, 77, 78, 79};
        check(ms.laserTotal() == 1 && ms.laserDone() == 1 && lines == want && bombs.size() == 1 &&
                  glm::distance(glm::vec2(bombs[0].x, bombs[0].z), x) < 50.0f,
              "mission: laser designation, the MiG-27's bombs on X", double(lines.size()));
      }

      // Troops as the missions carry them: land at each landing zone, doors open until the
      // flight engineer is done, then home; deploying (SARATT16), evacuating (SARATT17),
      // casualties (SARATT18), three pilots at three places (KOREAS06), troops out and
      // casualties in at one place with the wingmen doing the same (AFGHNC19).
      for (const char* name : {"saratt16", "saratt17", "saratt18", "koreas06", "afghnc19"}) {
        gamedata::Mission m;
        if (!gamedata::loadMission(dir, name, m, zoneError) || !formOk) continue;
        static const char* zoneDirs[] = {"", "KAZAK.WZ", "KOREA.WZ", "AFGHAN.WZ", "SARATOV.WZ"};
        gamedata::Zone mz;
        if (m.zone < 1 || m.zone > 4 || !gamedata::loadZone(dir, zoneDirs[m.zone], mz, zoneError)) continue;
        const glm::vec2 origin = m.player()->route[0].pos;
        world::Terrain land(mz.heightMap(origin, 300.0f));
        sim::Targets units(land);
        sim::MissionState ms;
        sim::Troops tr;
        ms.start(m, formations, original, origin, units);
        tr.start(ms.passengers(), ms.carriers(), m.humansDeployed, m.humansLoaded, ms.route()[0], units);
        auto h = std::make_unique<Helicopter>(land);
        sim::Controls idle;
        const int zones = int(tr.zonesLeft().size());
        bool wrong = false;
        auto wait = [&](float s) {
          for (int k = 0; k < int(s * 30.0f); ++k) {
            h->step(idle, 1.0f / 30.0f);
            units.step(1.0f / 30.0f);
            tr.step(*h, units, land, 1.0f / 30.0f);
            for (int id : tr.takeRadio()) wrong |= id == gamedata::snd::WrongDeploymentArea || id == gamedata::snd::WrongBoardingArea;
            ms.setTroopsTask(tr.deployTask() || tr.evacuationTask(), tr.deployed() && tr.evacuated());
            ms.step(*h, units, 1.0f / 30.0f);
          }
        };
        h->resetAirborne({ms.route()[0].x, land.groundHeight(ms.route()[0].x, ms.route()[0].y) + 100.0f, ms.route()[0].y}, 0.0f, 0.0f);
        ms.step(*h, units, 61.0f);  // out of the base
        for (int visit = 0; visit < 6 && !tr.zonesLeft().empty(); ++visit) {
          const glm::vec2 z = tr.zonesLeft().front() + glm::vec2(40.0f, 0.0f);
          h->resetOnGround(z, 0.0f);
          wait(2.0f);
          tr.toggleDoors(*h);
          wait(240.0f);
          tr.toggleDoors(*h);
        }
        const int left = int(tr.zonesLeft().size());
        h->resetOnGround(ms.route()[0], 0.0f);
        wait(40.0f);
        char what[160];
        std::snprintf(what, sizeof what, "troops: %s, %d zone(s), %d of %d out, %d of %d in, back home = success", name, zones,
                      tr.deployedMen(), tr.deployNeeded(), tr.loadedMen(), tr.loadNeeded());
        check(zones > 0 && left == 0 && !wrong && tr.deployNeeded() + tr.loadNeeded() > 0 && ms.finished() &&
                  ms.result() >= sim::MissionResult::Success,
              what, int(ms.result()));
      }

      {
        // The autorotation drill: the result by where it stops and how the rotor came down.
        gamedata::addMissionDir(HIND_DATA_DIR "/missions");
        gamedata::Mission drill;
        check(gamedata::loadMission(dir, "saratt25", drill, zoneError), "drill: the mission is there", 0);
        if (drill.autorotation > 0) {
          const auto landing = [&](bool free, float cutAt, float off) {
            sim::Targets none(terrain);
            sim::MissionState ms;
            ms.start(drill, formations, original, glm::vec2(0.0f), none);
            sim::Helicopter h2(terrain);
            const glm::vec2 home = ms.route()[0];
            h2.resetAirborne({home.x + off, terrain.groundHeight(home.x + off, home.y) + cutAt, home.y}, 0.0f, 40.0f);
            h2.rotorFree = free;
            ms.step(h2, none, 0.01f);
            check(ms.result() == sim::MissionResult::Flying, "drill: flying till it stops", int(ms.result()));
            h2.pos.y = terrain.groundHeight(h2.pos.x, h2.pos.z) + sim::Helicopter::kRestHeight;
            h2.weightOnWheels = 1.0f, h2.vel = glm::vec3(0.0f);
            ms.step(h2, none, 0.01f);
            return ms.result();
          };
          check(drill.autorotation == 900, "drill: the mission starts high", drill.autorotation);
          check(landing(true, 500.0f, 0.0f) == sim::MissionResult::Outstanding, "drill: unpowered at the base: outstanding", 0);
          check(landing(true, 500.0f, 1500.0f) == sim::MissionResult::Success, "drill: unpowered elsewhere: success", 0);
          check(landing(false, 500.0f, 0.0f) == sim::MissionResult::Failed, "drill: under power: failure", 0);
          check(landing(true, 40.0f, 0.0f) == sim::MissionResult::Failed, "drill: cut only near the ground: failure", 0);
        }
      }

      {
        // The mission's own stores: KOREAS06 UPK-23, R-60 and 9M114; SARATT14 two KMGU-2 only.
        gamedata::Mission a, b;
        sim::Loadout la, lb;
        const bool ok = gamedata::loadMission(dir, "koreas06", a, zoneError) && gamedata::loadMission(dir, "saratt14", b, zoneError) &&
                        sim::missionLoadout(a, la) && sim::missionLoadout(b, lb);
        check(ok && la.inner == sim::Store::Upk23 && la.outer == sim::Store::R60 && la.wingtip == sim::Store::Shturm &&
                  lb.inner == sim::Store::Kmgu && lb.outer == sim::Store::None && lb.wingtip == sim::Store::None,
              "mission: stores as the mission loads them", 0);
        // The door guns: none in the original's missions (the one chosen before goes), both where
        // the Shaheds are hunted; a door gun's mass counted.
        sim::Loadout lc, ld;
        const bool parsed = sim::parseDoorGuns("right", lc) && !sim::parseDoorGuns("middle", lc);
        const float mass = lc.mass();
        sim::missionLoadout(a, lc);
        gamedata::addMissionDir(HIND_DATA_DIR "/missions");
        gamedata::Mission u;
        const bool hunt = gamedata::loadMission(dir, "ukraic01", u, zoneError) && sim::missionLoadout(u, ld);
        check(parsed && std::string(sim::doorGunsKey(lc)) == "none" && hunt && std::string(sim::doorGunsKey(ld)) == "both" &&
                  mass - sim::Loadout{}.mass() == sim::kDoorGunMass,
              "mission: door guns by the mission, their mass counted", mass);
      }
      {
        // KOREAS03 "Decapitation": its target X is the government building in Seoul (a building
        // of the zone, not a unit of the mission), as the original's waypoint information says.
        gamedata::Mission m;
        gamedata::Zone kz;
        if (gamedata::loadMission(dir, "koreas03", m, zoneError) && gamedata::loadZone(dir, "KOREA.WZ", kz, zoneError)) {
          const std::vector<std::string> names = sim::waypointTargets(m, original, kz.objects);
          int x = -1;
          for (size_t i = 0; i < m.player()->route.size() && x < 0; ++i)
            if (m.player()->route[i].type == 4) x = int(i);
          check(x >= 0 && names.size() == m.player()->route.size() && names[size_t(x)] == "Government Building" && names[0].empty(),
                "mission: target waypoint X named after the government building", x);
        }
        // KOREAS02: the radar sites at its target points (the zone's buildings) are its primary
        // targets, half of them needed; the game's own air defences stand in both sides' cells.
        gamedata::Mission r;
        if (gamedata::loadMission(dir, "koreas02", r, zoneError) && kz.size > 0) {
          world::Terrain flat;
          sim::Targets t(flat);
          sim::MissionState ms;
          ms.start(r, formations, original, r.player()->route[0].pos, t, &kz.objects);
          int radars = 0, ourAa = 0, theirAa = 0;
          for (const sim::Vehicle& v : t.list) {
            const sim::VehicleType& vt = sim::vehicleType(v.kind);
            radars += v.objective == sim::Objective::Success && vt.original == "EWR_Model_1";
            if (vt.original == "ZSU23_Shilka_AAA" && v.side == sim::Side::Ours) ++ourAa;
            if (vt.original == "M163_Vulcan" && v.side == sim::Side::Enemy) ++theirAa;
          }
          check(r.cellSides.size() == 64 * 64 && radars >= 1 && ms.successTotal() >= 2 && ms.neededPrimary() == (ms.successTotal() + 1) / 2 &&
                    ourAa > 10 && theirAa > 10,
                "mission: KOREAS02 radar sites primary, Gameplay AAA of both sides", ms.successTotal());
        }
        // The remake's own campaign: UKRAIC01 from data/missions, three Shaheds for the airbase,
        // the flight with its Mi-17; left alone, all three get through and fail it.
        gamedata::addMissionDir(HIND_DATA_DIR "/missions");
        gamedata::Mission u1;
        gamedata::Zone sz;
        if (gamedata::loadMission(dir, "ukraic01", u1, zoneError) && gamedata::loadZone(dir, "SARATOV.WZ", sz, zoneError)) {
          world::Terrain land(sz.heightMap(u1.player()->route[0].pos, 300.0f));
          sim::Targets t(land);
          sim::MissionState ms;
          ms.start(u1, formations, original, u1.player()->route[0].pos, t, &sz.objects);
          sim::Spotter spotter(land);
          auto h = std::make_unique<Helicopter>(land);
          h->resetAirborne({0.0f, 3000.0f, 0.0f}, 0.0f, 0.0f);
          for (int k = 0; k < 20 * 60 * 20; ++k) t.step(0.05f), ms.step(*h, t, 0.05f);
          check(gamedata::listMissions(dir, "UKRAIC").size() == 8 && ms.dronesTotal() == 3 && spotter.active(t) &&
                    ms.dronesStruck() == 3 && ms.result() == sim::MissionResult::Failed,
                "ukraine: UKRAIC01 three Shaheds, the Hip up, all through = failed", ms.dronesStruck());
        }
        // KOREAC14 needs 20 kills with 6 primary targets: the other targets count towards it.
        gamedata::Mission c14;
        if (gamedata::loadMission(dir, "koreac14", c14, zoneError)) {
          world::Terrain flat;
          sim::Targets t(flat);
          sim::MissionState ms;
          ms.start(c14, formations, original, c14.player()->route[0].pos, t);
          check(ms.neededPrimary() == ms.successTotal() && ms.neededTotal() == 20, "mission: KOREAC14 20 kills, all its primary targets among them",
                ms.neededTotal());
        }
        // The buildings' vertices are packed: the government building is 46 x 67 m, 22 m high.
        const gamedata::Model gov = original.model(original.find("Korean_Government_Bldg"));
        const glm::vec3 size = gov.hi - gov.lo;
        check(std::fabs(size.x - 45.6f) < 1.0f && std::fabs(size.y - 22.3f) < 1.0f && std::fabs(size.z - 66.9f) < 1.0f,
              "original: packed vertices, government building 46 x 67 m (high, m)", size.y);
      }
      if (jalalabad) {
        // Jalalabad's runway as its markings lay it out: east-west, 79 m north of the airfield's
        // point, some 1.8 km long.
        const std::vector<world::Runway> rw = gamedata::zoneRunways(zone, original, jalalabad->pos);
        const float length = rw.size() == 1 ? glm::distance(rw[0].a, rw[0].b) : 0.0f;
        check(rw.size() == 1 && std::fabs(rw[0].a.y + 79.0f) < 2.0f && std::fabs(rw[0].b.y + 79.0f) < 2.0f &&
                  length > 1750.0f && length < 1900.0f,
              "zone: Jalalabad's one runway east-west from its markings (m)", length);
      }
      {
        // Mine laying (SARATT14): a KMGU-2 compartment over each of X, Y and Z, low and fast, is a
        // good field each; then home is a success. One let go too high is off target.
        gamedata::Mission m;
        if (gamedata::loadMission(dir, "saratt14", m, zoneError) && formOk) {
          gamedata::Zone mz;
          if (gamedata::loadZone(dir, "SARATOV.WZ", mz, zoneError)) {
            const glm::vec2 origin = m.player()->route[0].pos;
            world::Terrain land(mz.heightMap(origin, 300.0f));
            sim::Targets units(land);
            sim::MissionState ms;
            ms.start(m, formations, original, origin, units);
            sim::Weapons w(land);
            sim::Loadout lo;
            lo.inner = sim::Store::Kmgu;
            lo.outer = sim::Store::None;
            w.load(lo);
            w.select(sim::WeaponType::Mine);
            auto h = std::make_unique<Helicopter>(land);
            std::vector<sim::Impact> imp;
            std::vector<int> said;
            auto drop = [&](glm::vec2 at, float height) {
              h->resetAirborne({at.x, land.groundHeight(at.x, at.y) + height, at.y}, 0.0f, 250.0f / 3.6f);
              w.step(*h, true, 0.01f, imp, &units);
              w.step(*h, false, 0.01f, imp, &units);
              for (const sim::MineRelease& r : w.mineReleases) said.push_back(ms.mineRelease(r));
              w.mineReleases.clear();
            };
            std::vector<glm::vec2> fields;
            for (size_t i = 0; i < m.player()->route.size(); ++i)
              if (m.player()->route[i].type == 17) fields.push_back(ms.route()[i]);
            drop(fields.empty() ? glm::vec2(0.0f) : fields[0], 400.0f);
            for (glm::vec2 f : fields) drop(f, 80.0f);
            for (int k = 0; k < 30 * 60; ++k) w.step(*h, false, 1.0f / 60.0f, imp, &units);
            int landed = 0;
            for (const sim::Impact& i : imp) landed += i.type == sim::WeaponType::Mine;
            h->resetAirborne({0.0f, land.groundHeight(0.0f, 0.0f) + 100.0f, 0.0f}, 0.0f, 0.0f);
            ms.step(*h, units, 61.0f);
            h->resetOnGround(ms.route()[0], 0.0f);
            ms.step(*h, units, 0.1f);
            const std::vector<int> want{-1, 1, 1, 1};
            check(fields.size() == 3 && said == want && ms.minesDone() == 3 && landed > 50 && ms.finished() &&
                      ms.result() >= sim::MissionResult::Success,
                  "mission: SARATT14 three minefields laid low and fast, one too high off target", ms.minesDone());
          }
        }
        // An enemy soldier walking onto a mine is blown up; standing by one he is not.
        sim::Targets units(terrain);
        const sim::VehicleKind man = sim::registerVehicleType({"SOLDIER", "Test_Human_Figure", sim::Armour::Soft, 1.0f, {-0.3f, 0, -0.3f}, {0.3f, 1.8f, 0.3f}, sim::UnitClass::Soldier});
        const int a = units.add(man, 100.0f, 0.0f, 0.0f), b = units.add(man, 120.0f, 0.0f, 0.0f);
        units.setRoute(a, {{{100.0f, 0.0f}}, {{100.0f, -20.0f}, 0.0f, 1.5f}}, false, 0.0f);
        units.addMine({100.0f, terrain.groundHeight(100.0f, -10.0f), -10.0f});
        units.addMine({120.5f, terrain.groundHeight(120.0f, 0.0f), 0.0f});
        for (int k = 0; k < 20 * 60; ++k) units.step(1.0f / 60.0f);
        const std::vector<int> kills = units.takeMineKills();
        check(kills.size() == 1 && kills[0] == a && units.list[size_t(a)].destroyed && !units.list[size_t(b)].destroyed &&
                  units.mines.size() == 1,
              "mines: the enemy walking onto one is blown up, the one standing by is not", double(kills.size()));
      }
      {
        // The villages of AFGHNS04 are ruins from the start (":Destroy"), not kills of the player's.
        gamedata::Mission m;
        if (gamedata::loadMission(dir, "afghns04", m, zoneError) && jalalabad && formOk) {
          world::Terrain land(zone.heightMap(m.player()->route[0].pos, 300.0f));
          sim::Targets units(land);
          sim::MissionState ms;
          ms.start(m, formations, original, m.player()->route[0].pos, units, &zone.objects);
          int ruins = 0;
          for (const sim::Vehicle& v : units.list) ruins += v.destroyed;
          int scenery = 0;
          for (const sim::Vehicle& v : units.list) scenery += v.scenery;
          check(!m.destroyed.empty() && ruins == int(m.destroyed.size()) && ms.otherKills() == 0 && scenery > 100,
                "mission: AFGHNS04 villages (war zone's buildings) in ruins from the start", ruins);
        }
      }

      // With the cockpit palette the shaded dial faces turn flat black, as in flight: count the
      // bluish shading pixels inside the airspeed dial before and after.
      gamedata::PaletteRange pal;
      std::string error;
      bool palOk = gamedata::loadPalette(gamedata::findOriginalFile(dir, "COCKPITS/COCKPITS.RGB"), 224, pal, error);
      auto shaded = [&](const gamedata::PaletteRange* replace) {
        gamedata::Picture pic;
        std::string e;
        if (!gamedata::loadPbm(gamedata::findOriginalFile(dir, "COCKPITS/PILOT.LBM"), pic, 0, e, replace)) return -1;
        int n = 0;
        for (int y = 332; y < 380; ++y)
          for (int x = 820; x < 868; ++x) {
            if ((x - 844) * (x - 844) + (y - 356) * (y - 356) > 24 * 24) continue;
            const uint8_t* p = &pic.rgba[(size_t(y) * pic.width + x) * 4];
            n += p[2] > p[0] + 15 && p[0] < 60;
          }
        return n;
      };
      int before = shaded(nullptr), after = shaded(&pal);
      check(palOk && pal.rgb.size() == 96 && before > 100 && after == 0, "original: cockpit palette flattens the dial faces",
            float(before));
      gamedata::Picture lamp;
      bool lampOk = gamedata::loadRaw(gamedata::findOriginalFile(dir, "COCKPITS/UCLDOWN.640"), 6, pal, lamp, error);
      bool green = false;
      for (size_t i = 0; i + 3 < lamp.rgba.size(); i += 4) green |= lamp.rgba[i + 1] > 150 && lamp.rgba[i] < 60;
      check(lampOk && lamp.width == 6 && lamp.height == 6 && green, "original: gear lamp sprite UCLDOWN.640 is lit green",
            double(lamp.width));
    }
  }

  flyCircuit(terrain, FlightModel::Stable, "stable");
  // Realistic: full pedal at 200 km/h with the wings held level holds a sideslip of some ten
  // degrees and starts a bank when the cyclic is let go; released, the fin straightens it out.
  for (bool hands : {true, false}) {
    Helicopter h(terrain);
    h.model = FlightModel::Realistic;
    glm::vec3 p0{af.apronCenter.x, terrain.groundHeight(af.apronCenter.x, af.apronCenter.y) + 300.0f, af.apronCenter.y};
    h.resetAirborne(p0, 0.0f, 200.0f / 3.6f);
    auto slip = [&] {
      glm::vec3 vb = glm::transpose(glm::mat3_cast(h.orient)) * h.vel;
      return glm::degrees(std::atan2(vb.x, -vb.z));
    };
    auto fly = [&](float pedal, float seconds) {
      for (int i = 0; i < int(seconds / kDt); ++i) {
        sim::Controls c;
        float pitch0 = 0;
        float base = Helicopter::levelFlightCollective(glm::length(h.vel), h.mass, &pitch0);
        c.cyclicY = std::clamp(-0.06f * (-glm::degrees(pitch0) - h.pitchDeg()) + 1.5f * h.omega.x, -1.0f, 1.0f);
        if (hands) c.cyclicX = std::clamp(-0.05f * h.rollDeg() + 0.8f * h.omega.z, -1.0f, 1.0f);
        c.collective = base - 0.08f * h.vel.y;
        c.pedal = pedal;
        h.step(c, kDt);
      }
    };
    fly(1.0f, 4.0f);
    if (hands) {
      float held = std::fabs(slip());
      check(held > 7.0f && held < 18.0f, "realistic: full pedal at 200 km/h, sideslip (deg)", held);
      fly(0.0f, 5.0f);
      check(std::fabs(slip()) < 2.0f, "realistic: pedal released, fin straightens it (deg)", std::fabs(slip()));
    } else {
      check(h.rollDeg() > 5.0f, "realistic: pedal at speed rolls towards it (deg)", h.rollDeg());
    }
  }

  flyCircuit(terrain, FlightModel::Realistic, "realistic");

  {
    // Troops: eight men get out at the drop zone and walk off; at the evacuation point a casualty
    // comes over on its stretcher with its two carriers and climbs in.
    auto h = std::make_unique<Helicopter>(terrain);
    h->resetOnGround({0.0f, 0.0f}, 0.0f);
    sim::Controls idle;
    run(*h, idle, 2.0f);
    sim::Targets units(terrain);
    const sim::VehicleKind man = sim::registerVehicleType({"SOLDIER", "Test_Human_Figure", sim::Armour::Soft, 1.0f, {-0.3f, 0, -0.3f}, {0.3f, 1.8f, 0.3f}, sim::UnitClass::Soldier});
    const sim::VehicleKind hurt = sim::registerVehicleType({"CASUALTY", "Casualty_on_Stretcher", sim::Armour::Soft, 1.0f, {-0.3f, 0, -1.2f}, {0.3f, 0.4f, 1.2f}, sim::UnitClass::Soldier});
    for (float x : {28.0f, 29.0f, 30.0f}) {
      int i = units.add(x == 29.0f ? hurt : man, x, 10.0f, 0.0f);
      units.list[size_t(i)].side = sim::Side::Ours;
    }
    sim::Troops tr;
    sim::PassengerGroup drop{1, true, {0.0f, 0.0f}, {}, sim::Troops::kCabin, false}, pickup{1, false, {0.0f, 0.0f}, {0, 1, 2}, 0, false};
    tr.start({drop, pickup}, {}, 8, 3, {5000.0f, 5000.0f}, units);
    std::vector<int> said;
    tr.toggleDoors(*h);
    for (int k = 0; k < 120 * 60; ++k) {
      tr.step(*h, units, terrain, 1.0f / 60.0f);
      for (int id : tr.takeRadio()) said.push_back(id);
    }
    int out = 0;
    for (const sim::Walker& w : tr.walkers()) out += w.goal == sim::Walker::Goal::Stand && glm::distance(w.pos, h->pos) > 12.0f;
    auto heard = [&](int id) { return std::find(said.begin(), said.end(), id) != said.end(); };
    check(tr.deployed() && out == 8 && heard(gamedata::snd::TroopDeployed), "troops: 8 men out at the drop zone, walked off", out);
    check(tr.evacuated() && tr.aboard() == 1 && tr.loadedMen() == 3 && units.list[0].aboard && units.list[1].aboard && units.list[2].aboard &&
              heard(gamedata::snd::CasualtiesAboard),
          "troops: the casualty and its two carriers climb in", tr.loadedMen());
  }

  {
    // Time of day: the noon sun high in the south; at half past eight in the evening still dusk
    // as in the original (the ground some 40 per cent lit, a grey-blue sky, no red, no stars, the
    // cockpit in full light); a starry night lit faintly by the moon; thick fog closes in to
    // some 1.2 km.
    const render::Palette noon = render::skyAt(12.0f, 0), dusk = render::skyAt(20.5f, 0), night = render::skyAt(23.5f, 0),
                          fog = render::skyAt(12.0f, 3);
    const float dayLight = noon.ambient + noon.diffuse, nightLight = night.ambient + night.diffuse;
    const float duskLight = (dusk.ambient + dusk.diffuse) / dayLight;
    check(noon.sunDir.y > 0.8f && noon.sunDir.z > 0.3f && noon.stars == 0.0f && dusk.light == glm::vec3(1.0f) &&
              duskLight > 0.3f && duskLight < 0.5f && dusk.stars == 0.0f && dusk.cockpit == glm::vec3(1.0f) &&
              night.stars == 1.0f && nightLight < dayLight * 0.4f && night.horizon.r < 0.1f && fog.fogDist < 1500.0f &&
              noon.fogDist > 1e8f,
          "daylight: noon sun south, grey dusk at 20:30, dark starry night, thick fog", duskLight);
  }
  {
    // An overcast sunrise as in the original's SARATT04: the overcast sky and the ground evenly
    // about half as bright as by day, no red in the light, the cockpit in full light; at night
    // only the instruments' lamps.
    const render::SkyColours sky;
    const render::Palette dawn = render::skyAt(6.0f, 1, sky), day = render::skyAt(12.0f, 1, sky), night = render::skyAt(23.0f, 1, sky);
    const float half = dawn.zenith.b / sky.overcastZenith.b, ground = (dawn.ambient + dawn.diffuse) / (day.ambient + day.diffuse);
    check(half > 0.4f && half < 0.6f && ground > 0.4f && ground < 0.6f && dawn.light == glm::vec3(1.0f) &&
              dawn.cockpit == glm::vec3(1.0f) && glm::distance(day.zenith, sky.overcastZenith) < 0.01f && night.cockpit.r < 0.5f,
          "daylight: overcast dawn evenly half dark, cockpit lit", half);
  }

  {
    // Hunting a Shahed-136: it flies east at 180 km/h, 150 m up. A Mi-17 of ours 5 km off sees
    // it and calls it out from the player's helicopter following it 300 m behind, 150 m to its
    // left and a little higher (the turret turns 60 degrees either way); the WSO takes it and lays
    // the Yak-B on it with lead: down within seconds.
    world::Terrain flat;
    sim::Targets t(flat);
    const float dt = 1.0f / 120.0f;
    const int drone = t.add(sim::VehicleKind::Shahed, -2000.0f, 0.0f, glm::radians(90.0f));
    t.setRoute(drone, {{{-2000.0f, 0.0f}, 150.0f, 50.0f}, {{8000.0f, 0.0f}, 150.0f, 50.0f}}, false, 0.0f);
    const sim::VehicleKind hip = sim::registerVehicleType({"MI-17", "Mi_17_Test", sim::Armour::Light, 12.0f, {-1.5f, 0, -9}, {1.5f, 4, 9},
                                                           sim::UnitClass::Helicopter});
    const int mi17 = t.add(hip, -2000.0f, -5000.0f, 0.0f);
    t.list[size_t(mi17)].side = sim::Side::Ours;
    t.list[size_t(mi17)].pos.y += 300.0f;
    auto h = std::make_unique<Helicopter>(flat);
    h->resetAirborne({-2300.0f, flat.groundHeight(-2000.0f, 0.0f) + 200.0f, -150.0f}, glm::radians(90.0f), 50.0f);
    sim::Spotter spotter(flat);
    sim::Wso wso(flat);
    sim::Weapons w(flat);
    w.load(sim::Loadout{});
    w.selectNext();  // armed, the gun
    std::vector<sim::Impact> imp;
    std::string call;
    bool wsoSpoke = false;
    float sec = 0.0f;
    for (; sec < 30.0f && !t.list[size_t(drone)].destroyed; sec += dt) {
      t.step(dt);
      h->pos += glm::vec3(50.0f, 0.0f, 0.0f) * dt;  // alongside, at its speed
      spotter.step(*h, t, dt);
      if (call.empty()) call = spotter.line(*h, t);
      if (h->message.rfind("WSO: ", 0) == 0) wsoSpoke = true;  // (silent: the call says which already)
      if (wso.target() < 0) wso.designate(*h, t, spotter.designated());
      wso.update(*h, w, t, dt);
      w.step(*h, wso.target() == drone, dt, imp, &t);
    }
    check(call.rfind("HIP: SHAHED 1 O'CLOCK 0.3 KM", 0) == 0, "drones: the Mi-17 calls the Shahed out from the player", call.size());
    check(!wsoSpoke, "drones: the WSO takes the Hip's drone without a word", 0);
    {
      // The drone down and no other in sight: the Hip says so at once.
      spotter.step(*h, t, dt);
      check(spotter.line(*h, t) == "HIP: NO MORE CONTACTS", "drones: after the kill the Hip says there are no more", 0);
    }
    {
      // Another drone, called out; then the Hip shot down: no more calls, the one made is void.
      const int second = t.add(sim::VehicleKind::Shahed, -1000.0f, -3000.0f, glm::radians(90.0f));
      t.setRoute(second, {{{-1000.0f, -3000.0f}, 150.0f, 50.0f}, {{9000.0f, -3000.0f}, 150.0f, 50.0f}}, false, 0.0f);
      sim::Spotter again(flat);
      for (float s2 = 0.0f; s2 < 2.0f; s2 += dt) t.step(dt), again.step(*h, t, dt);
      const bool called = again.designated() == second;
      t.list[size_t(mi17)].destroyed = true;
      for (float s2 = 0.0f; s2 < 2.0f; s2 += dt) t.step(dt), again.step(*h, t, dt);
      check(called && again.designated() < 0 && again.line(*h, t).empty(), "drones: with the Mi-17 down nobody calls the drones",
            again.designated());
    }
    check(t.list[size_t(drone)].destroyed && !t.list[size_t(drone)].struck, "drones: WSO and Yak-B bring a Shahed down alongside (s)", sec);
    // The door gunner: the Shahed flying alongside 250 m to the right and below, as the crews
    // hunt them; the gunner in the right window brings it down by himself, one in the left
    // window cannot reach it, and the right one told to hold fire (G) leaves it alone.
    for (int run = 0; run < 3; ++run) {
      const int side = std::min(run, 1);
      sim::Targets u(flat);
      const int d = u.add(sim::VehicleKind::Shahed, -2000.0f, 0.0f, glm::radians(90.0f));
      u.setRoute(d, {{{-2000.0f, 0.0f}, 150.0f, 50.0f}, {{8000.0f, 0.0f}, 150.0f, 50.0f}}, false, 0.0f);
      auto hh = std::make_unique<Helicopter>(flat);
      hh->resetAirborne({-2000.0f, u.list[size_t(d)].pos.y + 80.0f, -250.0f}, glm::radians(90.0f), 50.0f);
      sim::Weapons ww(flat);
      sim::Loadout lo;
      lo.doorGun[side] = true;
      ww.load(lo);
      ww.doorGunnersFree = run < 2;
      std::vector<sim::Impact> im;
      float sec = 0.0f;
      for (; sec < 30.0f && !u.list[size_t(d)].destroyed; sec += dt) {
        u.step(dt);
        hh->pos += glm::vec3(50.0f, 0.0f, 0.0f) * dt;
        ww.step(*hh, false, dt, im, &u);
      }
      if (run == 2)
        check(!u.list[size_t(d)].destroyed && ww.doorRounds(1) == sim::kDoorGunRounds, "drones: door gunners holding fire do not fire", ww.doorRounds(1));
      else if (side == 1)
        check(u.list[size_t(d)].destroyed && ww.doorRounds(1) < sim::kDoorGunRounds, "drones: the right door gunner downs a Shahed alongside (s)", sec);
      else
        check(!u.list[size_t(d)].destroyed && ww.doorRounds(0) == sim::kDoorGunRounds, "drones: the left one cannot see it", ww.doorRounds(0));
    }
    {
      // Closing in on it from 350 m behind, 8 m/s faster than it flies: the right door gunner
      // waits while the shot keeps getting better (the first 7 s), then fires (once it stops
      // getting better: alongside, or the drone wandering off its height).
      sim::Targets u(flat);
      const int d = u.add(sim::VehicleKind::Shahed, -2000.0f, 0.0f, glm::radians(90.0f));
      u.setRoute(d, {{{-2000.0f, 0.0f}, 150.0f, 50.0f}, {{12000.0f, 0.0f}, 150.0f, 50.0f}}, false, 0.0f);
      auto hh = std::make_unique<Helicopter>(flat);
      hh->resetAirborne({-2350.0f, u.list[size_t(d)].pos.y + 80.0f, -150.0f}, glm::radians(90.0f), 58.0f);
      sim::Weapons ww(flat);
      sim::Loadout lo;
      lo.doorGun[1] = true;
      ww.load(lo);
      std::vector<sim::Impact> im;
      int closingFired = -1;
      float sec = 0.0f;
      for (; sec < 60.0f && !u.list[size_t(d)].destroyed; sec += dt) {
        u.step(dt);
        const float behind = u.list[size_t(d)].pos.x - hh->pos.x;
        hh->vel = glm::vec3(u.list[size_t(d)].vel.x + (behind > 0.0f ? 8.0f : 0.0f), 0.0f, 0.0f);
        hh->pos += hh->vel * dt;
        ww.step(*hh, false, dt, im, &u);
        if (closingFired < 0 && sec >= 7.0f) closingFired = sim::kDoorGunRounds - ww.doorRounds(1);
      }
      check(closingFired == 0 && u.list[size_t(d)].destroyed, "drones: the door gunner waits while closing in, then downs it (s)", sec);
    }
    // Left alone it dives onto the end of its route.
    sim::Targets u(flat);
    const int lone = u.add(sim::VehicleKind::Shahed, 0.0f, 0.0f, glm::radians(90.0f));
    u.setRoute(lone, {{{0.0f, 0.0f}, 150.0f, 50.0f}, {{2000.0f, 0.0f}, 150.0f, 50.0f}}, false, 0.0f);
    for (int k = 0; k < 60 * 60; ++k) u.step(1.0f / 60.0f);
    const std::vector<glm::vec3> strikes = u.takeStrikes();
    check(u.list[size_t(lone)].struck && strikes.size() == 1 && std::fabs(strikes[0].x - 2000.0f) < 1.0f,
          "drones: a Shahed left alone strikes the end of its route", strikes.size());
    {
      // No two alike: drones launched from different places fly at different speeds and heights;
      // each dives onto its target at the end, 35 degrees down.
      sim::Targets r(flat);
      float lowAt100 = 1e9f, speeds[2] = {0, 0}, top[2] = {0, 0}, bottom[2] = {1e9f, 1e9f};
      for (int n = 0; n < 2; ++n) {
        const float y0 = n * 3000.0f;
        const int i = r.add(sim::VehicleKind::Shahed, -9000.0f, y0, glm::radians(90.0f));
        r.setRoute(i, {{{-9000.0f, y0}, 150.0f, 50.0f}, {{0.0f, y0}, 150.0f, 50.0f}}, false, 0.0f);
      }
      for (int k = 0; k < 240 * 60 && !(r.list[0].destroyed && r.list[1].destroyed); ++k) {
        r.step(1.0f / 60.0f);
        for (int n = 0; n < 2; ++n) {
          const sim::Vehicle& v = r.list[size_t(n)];
          if (v.destroyed) continue;
          if (k == 60 * 30) speeds[n] = glm::length(v.vel);
          if (v.pos.x < -2000.0f) top[n] = std::max(top[n], v.pos.y), bottom[n] = std::min(bottom[n], v.pos.y);
          if (n == 0 && std::fabs(v.pos.x + 100.0f) < 2.0f) lowAt100 = v.pos.y - flat.groundHeight(v.pos.x, v.pos.z);
        }
      }
      check(std::fabs(speeds[0] - speeds[1]) > 0.5f && std::fabs(top[0] - top[1]) > 3.0f && top[0] - bottom[0] > 20.0f,
            "drones: each flies its own speed and wandering height (m/s apart)", std::fabs(speeds[0] - speeds[1]));
      check(lowAt100 < 90.0f && r.list[0].struck && r.list[1].struck, "drones: the dive at 35 degrees, 100 m out (m up)", lowAt100);
    }
    {
      // The jet one, the Geran-3: some 330 km/h on its way, opening up to some 550 in its dive,
      // and it strikes the end of its route like a Shahed.
      sim::Targets r(flat);
      const int jet = r.add(sim::VehicleKind::Geran3, -9000.0f, 0.0f, glm::radians(90.0f));
      r.setRoute(jet, {{{-9000.0f, 0.0f}, 200.0f, 92.0f}, {{0.0f, 0.0f}, 200.0f, 92.0f}}, false, 0.0f);
      float cruise = 0.0f, dive = 0.0f;
      for (int k = 0; k < 120 * 60 && !r.list[size_t(jet)].destroyed; ++k) {
        r.step(1.0f / 60.0f);
        const sim::Vehicle& v = r.list[size_t(jet)];
        if (k == 30 * 60) cruise = glm::length(v.vel);
        if (v.pos.x > -500.0f && !v.destroyed) dive = std::max(dive, glm::length(v.vel));
      }
      check(cruise * 3.6f > 300.0f && cruise * 3.6f < 370.0f && dive * 3.6f > 500.0f && r.list[size_t(jet)].struck,
            "drones: a Geran-3 cruises at some 330 km/h and dives at some 550 (km/h)", dive * 3.6f);
      // The R-60's seeker sees the jet's nozzle from 7 km; a Shahed's piston engine only from
      // behind out to 1.5 km, from ahead 600 m.
      sim::Targets s(flat);
      const int pist = s.add(sim::VehicleKind::Shahed, 0.0f, 0.0f, glm::radians(90.0f));  // flying east (+x)
      const int jt = s.add(sim::VehicleKind::Geran3, 0.0f, 0.0f, glm::radians(90.0f));
      const sim::Vehicle &pv = s.list[size_t(pist)], &jv = s.list[size_t(jt)];
      const glm::vec3 behind{-1200.0f, 0.0f, 0.0f}, ahead{1200.0f, 0.0f, 0.0f}, farAhead{5000.0f, 0.0f, 0.0f};
      check(sim::Weapons::r60LockRange(pv, behind) > 1200.0f && sim::Weapons::r60LockRange(pv, ahead) < 1200.0f &&
                sim::Weapons::r60LockRange(jv, farAhead) > 5000.0f,
            "drones: R-60 locks a Shahed only close from behind, a Geran-3 from far ahead (m ahead)",
            sim::Weapons::r60LockRange(pv, ahead));
      check(std::string(sim::vehicleType(sim::VehicleKind::Geran3).original) == "Geran_3" && sim::isJetDrone(sim::VehicleKind::Geran3) &&
                !sim::isJetDrone(sim::VehicleKind::Shahed),
            "drones: the Geran-3 by its mission name, a jet", 0);
    }
    {
      // The Banderol: some 500 km/h without weaving, 650 in its dive; the R-60 sees it from 7 km,
      // an interceptor drone (slower than it) is not sent after it.
      sim::Targets r(flat);
      const int b = r.add(sim::VehicleKind::Banderol, -12000.0f, 0.0f, glm::radians(90.0f));
      r.setRoute(b, {{{-12000.0f, 0.0f}, 100.0f, 139.0f}, {{0.0f, 0.0f}, 100.0f, 139.0f}}, false, 0.0f);
      float cruise = 0.0f, dive = 0.0f, off = 0.0f;
      for (int k = 0; k < 120 * 60 && !r.list[size_t(b)].destroyed; ++k) {
        r.step(1.0f / 60.0f);
        const sim::Vehicle& v = r.list[size_t(b)];
        if (k == 30 * 60) cruise = glm::length(v.vel);
        if (k > 5 * 60 && v.pos.x < -2000.0f) off = std::max(off, std::fabs(v.pos.z));
        if (v.pos.x > -500.0f && !v.destroyed) dive = std::max(dive, glm::length(v.vel));
      }
      const sim::Vehicle& v = r.list[size_t(b)];
      check(cruise * 3.6f > 470.0f && cruise * 3.6f < 530.0f && dive * 3.6f > 600.0f && off < 2.0f && v.struck,
            "banderol: cruises at some 500 km/h straight on its way and dives at some 650 (km/h)", dive * 3.6f);
      sim::Targets s(flat);
      const int k = s.add(sim::VehicleKind::Banderol, 0.0f, 0.0f, glm::radians(90.0f));
      check(std::string(sim::vehicleType(sim::VehicleKind::Banderol).original) == "Banderol" && sim::isJetDrone(sim::VehicleKind::Banderol) &&
                sim::Weapons::r60LockRange(s.list[size_t(k)], {6000.0f, 0.0f, 0.0f}) > 6000.0f &&
                !sim::Weapons::interceptorTarget(s.list[size_t(k)]) && sim::arcadePoints(s.list[size_t(k)]) > 150,
            "banderol: by its name, R-60 locks it from 6 km ahead, no interceptor after it, worth more", 0);
      // The WSO with the R-60 selected: the Banderol 6 km ahead before a BMP at 2 km, and the
      // seeker locks on it as his target only.
      s.list[size_t(k)].pos.y = flat.groundHeight(0.0f, 0.0f) + 100.0f;
      const int bmp = s.add(sim::VehicleKind::Bmp2, 4000.0f, 0.0f, 0.0f);
      auto hh = std::make_unique<Helicopter>(flat);
      hh->resetAirborne({6000.0f, flat.groundHeight(6000.0f, 0.0f) + 100.0f, 0.0f}, -glm::half_pi<float>(), 30.0f);
      sim::Weapons ww(flat);
      sim::Wso ws(flat);
      ws.update(*hh, ww, s, 0.01f);
      const int plain = ws.target();
      ws.reset();
      ws.airFirst = true;
      ws.update(*hh, ww, s, 0.01f);
      check(plain == bmp && ws.target() == k && sim::Weapons::r60Seek(*hh, s, ws.target()) == k && sim::Weapons::r60Seek(*hh, s, bmp) == -1,
            "banderol: the WSO takes it at 6 km with the R-60 selected, the seeker locks it", ws.target());
      // Our AI Hinds over Ukraine carry what the player does not: R-60s on him, interceptors on
      // them; interceptors on him, R-60s on the wingman, who downs the Banderol coming at the flight.
      const sim::VehicleKind mi24 = sim::registerVehicleType({"MI-24 TEST", "Mi_24_Test", sim::Armour::Light, 14.0f, {-1.5f, 0, -9.0f}, {1.5f, 4.0f, 9.0f}, sim::UnitClass::Helicopter});
      sim::Targets fl(flat);
      const int wing = fl.add(mi24, 0.0f, 0.0f, 0.0f), second = fl.add(mi24, 100.0f, 0.0f, 0.0f);
      for (int u : {wing, second}) {
        fl.list[size_t(u)].side = sim::Side::Ours;
        fl.setRoute(u, {{{fl.list[size_t(u)].pos.x, 0.0f}, 150.0f, 30.0f}, {{fl.list[size_t(u)].pos.x, -20000.0f}, 150.0f, 30.0f}}, false, 0.0f);
      }
      for (int k2 = 0; k2 < 300; ++k2) fl.step(0.02f);
      sim::Loadout mine;
      mine.outer = sim::Store::R60;
      mine.inner = sim::Store::None;
      sim::WingArms arms(flat);
      arms.arm(fl, mine, false, false);
      const bool interceptorsOnThem = arms.hinds().size() == 2 && arms.hinds()[0].weapons->rounds(sim::WeaponType::Interceptor) > 0 &&
                                      arms.hinds()[0].weapons->rounds(sim::WeaponType::R60) == 0;
      mine.outer = sim::Store::None;
      mine.inner = sim::Store::Interceptor;
      arms.arm(fl, mine, false, false);
      const bool r60OnWingman = arms.hinds()[0].weapons->rounds(sim::WeaponType::R60) == 4 &&
                                arms.hinds()[1].weapons->rounds(sim::WeaponType::Interceptor) > 0;
      const glm::vec3 lead = fl.list[size_t(wing)].pos;
      const int ban = fl.add(sim::VehicleKind::Banderol, lead.x + 300.0f, lead.z - 9000.0f, 0.0f);
      fl.list[size_t(ban)].side = sim::Side::Enemy;
      fl.setRoute(ban, {{{lead.x + 300.0f, lead.z - 9000.0f}, 100.0f, 139.0f}, {{lead.x + 300.0f, lead.z + 30000.0f}, 100.0f, 139.0f}}, false, 0.0f);
      std::vector<sim::Impact> imp2;
      float when = 0.0f;
      for (; when < 60.0f && !fl.list[size_t(ban)].destroyed; when += 0.02f) {
        fl.step(0.02f);
        arms.step(fl, nullptr, 0.02f, imp2);
      }
      check(interceptorsOnThem && r60OnWingman && fl.list[size_t(ban)].destroyed && fl.list[size_t(ban)].killer == sim::Killer::Wingman,
            "wing arms: the stores the player has not; the wingman's R-60 downs a Banderol (s)", when);
      // The second, with interceptors, sends one at a Shahed coming at the flight (it has no door
      // gunner at it as the player's Hind has); what it carries goes to drawing (and the network).
      const glm::vec3 at2 = fl.list[size_t(second)].pos;
      const int sh = fl.add(sim::VehicleKind::Shahed, at2.x, at2.z - 2500.0f, 0.0f);
      fl.list[size_t(sh)].side = sim::Side::Enemy;
      fl.setRoute(sh, {{{at2.x, at2.z - 2500.0f}, 100.0f, 50.0f}, {{at2.x, at2.z + 30000.0f}, 100.0f, 50.0f}}, false, 0.0f);
      bool launched = false;
      for (float t = 0.0f; t < 20.0f && !launched; t += 0.02f) {
        fl.step(0.02f);
        arms.step(fl, nullptr, 0.02f, imp2);
        const auto& sh2 = arms.hinds()[1].weapons->shots;
        launched = std::any_of(sh2.begin(), sh2.end(), [&](const sim::Projectile& p) { return p.type == sim::WeaponType::Interceptor && p.target == sh; });
      }
      const std::vector<sim::WingArms::Stores> st = arms.stores();
      check(launched && st.size() == 2 && st[1].index == second && st[1].store[int(sim::Pylon::Inner)] == uint8_t(sim::Store::Interceptor) &&
                st[1].rounds[int(sim::Pylon::Inner)][0] + st[1].rounds[int(sim::Pylon::Inner)][1] < 12 && !arms.shots().empty(),
            "wing arms: an AI Hind's crew sends an interceptor at a Shahed", st.size());
    }
    {
      // A Gerbera decoy flies like a Shahed and comes down where it goes, harming nothing.
      sim::Targets r(flat);
      const int decoy = r.add(sim::VehicleKind::Gerbera, 0.0f, 0.0f, glm::radians(90.0f));
      const int truck = r.add(sim::VehicleKind::FuelTruck, 2000.0f, 5.0f, 0.0f);
      r.setRoute(decoy, {{{0.0f, 0.0f}, 150.0f, 50.0f}, {{2000.0f, 0.0f}, 150.0f, 50.0f}}, false, 0.0f);
      for (int k = 0; k < 60 * 60; ++k) r.step(1.0f / 60.0f);
      const sim::Vehicle& v = r.list[size_t(decoy)];
      check(v.destroyed && v.spent && !v.struck && r.takeStrikes().empty() && r.list[size_t(truck)].health == sim::vehicleType(sim::VehicleKind::FuelTruck).health,
            "drones: a Gerbera decoy comes down harmlessly", r.list[size_t(truck)].health);
    }
  }

  {
    // The enemy fights our units: a DShK wears down a truck of ours 800 m away within two
    // minutes; one 1.5 km away is out of its reach, and our own guns do not fire at ours.
    world::Terrain flat;
    sim::Targets units(flat);
    const sim::VehicleKind dshk = sim::registerVehicleType({"TEST DSHK", "Test_DShK", sim::Armour::Soft, 2.0f, {-1, 0, -1}, {1, 1.5f, 1},
                                                            sim::UnitClass::Vehicle, 0, 0.0f, -1, sim::Arms::Hmg});
    const sim::VehicleKind truck = sim::registerVehicleType({"TEST TRUCK", "Test_Truck", sim::Armour::Soft, 5.0f, {-1.2f, 0, -3}, {1.2f, 2.5f, 3},
                                                             sim::UnitClass::Vehicle});
    const int gun = units.add(dshk, -400.0f, 0.0f, 0.0f);
    const int near = units.add(truck, 400.0f, 0.0f, 0.0f), far = units.add(truck, -1900.0f, 0.0f, 0.0f);
    const int ownGun = units.add(dshk, 1800.0f, 0.0f, 0.0f);  // (out of reach of theirs both ways)
    units.list[size_t(near)].side = units.list[size_t(far)].side = units.list[size_t(ownGun)].side = sim::Side::Ours;
    units.list[size_t(gun)].health = units.list[size_t(ownGun)].health = 2.0f;
    units.list[size_t(near)].health = units.list[size_t(far)].health = 5.0f;
    sim::GroundBattle battle(flat);
    std::vector<sim::EnemyEvent> ev;
    for (int k = 0; k < 120 * 60; ++k) battle.step(units, glm::vec3(0.0f, 100.0f, 0.0f), 1.0f / 60.0f, ev);
    const bool quiet = std::none_of(ev.begin(), ev.end(), [](const sim::EnemyEvent& e) { return e.atHeli; });
    check(units.list[size_t(near)].destroyed && !units.list[size_t(far)].destroyed && !units.list[size_t(gun)].destroyed && quiet,
          "battle: enemy DShK destroys our truck at 800 m, not one at 1.5 km", double(ev.size()));
    // Ours fire back: a gun of ours 900 m from theirs silences it within a few minutes, though
    // less surely than theirs would.
    sim::Targets duel(flat);
    const int their = duel.add(dshk, -400.0f, 0.0f, 0.0f), our = duel.add(dshk, 500.0f, 0.0f, 0.0f);
    duel.list[size_t(our)].side = sim::Side::Ours;
    duel.list[size_t(their)].health = duel.list[size_t(our)].health = 6.0f;
    sim::GroundBattle fight(flat);
    std::vector<sim::EnemyEvent> ev2;
    int ourShots = 0;
    for (int k = 0; k < 300 * 60 && !duel.list[size_t(their)].destroyed && !duel.list[size_t(our)].destroyed; ++k) {
      fight.step(duel, glm::vec3(0.0f, 100.0f, 0.0f), 1.0f / 60.0f, ev2);
      for (const sim::EnemyEvent& e : ev2)
        if (e.type == sim::EnemyEvent::Muzzle && e.pos.x > 0.0f) ++ourShots;
      ev2.clear();
    }
    check(ourShots > 5 && (duel.list[size_t(their)].destroyed || duel.list[size_t(our)].destroyed),
          "battle: our gun fires back at theirs, one of them is silenced", ourShots);
  }

  {
    // R-60: fired with an enemy helicopter crossing 2 km ahead and tracked by the WSO, the
    // seeker locks on and the missile catches it; with nothing ahead, or the helicopter not
    // tracked, it has no lock (and flies unguided).
    sim::Targets units(terrain);
    const sim::VehicleKind cobra = sim::registerVehicleType({"AH-1 COBRA", "AH_1W_Cobra", sim::Armour::Light, 10.0f, {-1.5f, 0, -7.0f}, {1.5f, 3.5f, 7.0f}, sim::UnitClass::Helicopter});
    auto h = std::make_unique<Helicopter>(terrain);
    const float g = terrain.groundHeight(0.0f, -1000.0f);
    h->resetAirborne({0.0f, g + 150.0f, -1000.0f}, 0.0f, 50.0f);
    sim::Weapons w(terrain);
    sim::Loadout lo;
    lo.inner = sim::Store::None;
    lo.outer = sim::Store::R60;
    w.load(lo);
    const bool armed = w.select(sim::WeaponType::R60) && w.rounds(sim::WeaponType::R60) == 4;
    const int noLock = sim::Weapons::r60Seek(*h, units, 0);
    const int c = units.add(cobra, -600.0f, -3000.0f, glm::radians(90.0f));
    units.setRoute(c, {{{-600.0f, -3000.0f}, 150.0f, 60.0f}, {{3000.0f, -3000.0f}, 150.0f, 60.0f}}, false, 0.0f);
    const int untracked = sim::Weapons::r60Seek(*h, units, -1);
    const int lock = sim::Weapons::r60Seek(*h, units, c);
    std::vector<sim::Impact> imp;
    w.step(*h, true, 0.01f, imp, &units);
    const int unguided = w.shots.empty() ? -2 : w.shots.back().target;
    for (int k = 0; k < 200; ++k) w.step(*h, false, 0.01f, imp, &units);
    w.r60Track = c;
    w.step(*h, true, 0.01f, imp, &units);
    const int locked = w.shots.empty() ? -2 : w.shots.back().target;
    float t = 0.0f;
    for (; t < 15.0f && !units.list[size_t(c)].destroyed; t += 0.01f) {
      units.step(0.01f);
      w.step(*h, false, 0.01f, imp, &units);
    }
    check(armed && noLock == -1 && untracked == -1 && unguided == -1 && lock == c && locked == c && units.list[size_t(c)].destroyed,
          "R-60: locks on the crossing helicopter and downs it (s)", t);
  }

  {
    // Interceptor drones: the crew lets one go by itself at the Geran-3 coming head on only,
    // the gunner guiding it no other one and his gun silent till it is near; the Shahed crossing
    // 3 km off gets one at the pilot's order. The pilot cannot select them, and the gunners'
    // hold fire holds them too.
    sim::Targets units(terrain);
    auto h = std::make_unique<Helicopter>(terrain);
    const float g = terrain.groundHeight(0.0f, -1000.0f);
    h->resetAirborne({0.0f, g + 150.0f, -1000.0f}, 0.0f, 30.0f);
    sim::Weapons w(terrain);
    sim::Loadout lo;
    lo.inner = sim::Store::Interceptor;
    w.load(lo);
    w.selectNext();
    const bool notPilots = !w.armed() || w.selected() != sim::WeaponType::Interceptor;
    const int shahed = units.add(sim::VehicleKind::Shahed, -2000.0f, -4000.0f, glm::radians(90.0f));
    units.setRoute(shahed, {{{-2000.0f, -4000.0f}, 150.0f, 50.0f}, {{6000.0f, -4000.0f}, 150.0f, 50.0f}}, false, 0.0f);
    const int jet = units.add(sim::VehicleKind::Geran3, 0.0f, -5500.0f, glm::radians(180.0f));
    units.setRoute(jet, {{{0.0f, -5500.0f}, 200.0f, 92.0f}, {{0.0f, 20000.0f}, 200.0f, 92.0f}}, false, 0.0f);
    std::vector<sim::Impact> imp;
    w.doorGunnersFree = false;
    for (int k = 0; k < 100; ++k) units.step(0.01f), w.step(*h, false, 0.01f, imp, &units);
    const bool held = w.rounds(sim::WeaponType::Interceptor) == 12;
    w.doorGunnersFree = true;
    w.step(*h, false, 0.01f, imp, &units);
    const int first = w.shots.empty() ? -1 : w.shots.back().target;
    const int gunner = w.guiding(0) ? 0 : w.guiding(1) ? 1 : -1;
    float t = 0.0f;
    for (; t < 90.0f && !units.list[size_t(jet)].destroyed; t += 0.01f) {
      units.step(0.01f);
      w.step(*h, false, 0.01f, imp, &units);
    }
    const bool jetOnly = units.list[size_t(jet)].destroyed && !units.list[size_t(shahed)].destroyed &&
                         w.rounds(sim::WeaponType::Interceptor) == 11 && !w.guiding(0) && !w.guiding(1);
    const std::string order = w.orderInterceptor(*h);
    for (; t < 180.0f && !units.list[size_t(shahed)].destroyed; t += 0.01f) {
      units.step(0.01f);
      w.step(*h, false, 0.01f, imp, &units);
    }
    check(notPilots && held && first == jet && gunner >= 0 && jetOnly && order.empty() && units.list[size_t(shahed)].destroyed &&
              w.rounds(sim::WeaponType::Interceptor) == 10,
          "interceptors: the crew's at the jet only, the Shahed at the pilot's order, both down with two (s)", t);
  }

  {
    // The pilot's order goes at his target, not the nearest drone; one out of reach is refused.
    sim::Targets units(terrain);
    auto h = std::make_unique<Helicopter>(terrain);
    const float g = terrain.groundHeight(0.0f, -1000.0f);
    h->resetAirborne({0.0f, g + 150.0f, -1000.0f}, 0.0f, 30.0f);
    sim::Weapons w(terrain);
    sim::Loadout lo;
    lo.inner = sim::Store::Interceptor;
    w.load(lo);
    const int near = units.add(sim::VehicleKind::Shahed, 0.0f, -2500.0f, glm::radians(90.0f));
    const int far = units.add(sim::VehicleKind::Shahed, 1500.0f, -4500.0f, glm::radians(90.0f));
    const int away = units.add(sim::VehicleKind::Shahed, 0.0f, -9000.0f, glm::radians(90.0f));
    for (int k : {near, far, away}) {
      const glm::vec3 p = units.list[size_t(k)].pos;
      units.setRoute(k, {{{p.x, p.z}, 150.0f, 50.0f}, {{p.x + 8000.0f, p.z}, 150.0f, 50.0f}}, false, 0.0f);
    }
    std::vector<sim::Impact> imp;
    for (int k = 0; k < 10; ++k) units.step(0.01f), w.step(*h, false, 0.01f, imp, &units);
    const bool noneByThemselves = w.rounds(sim::WeaponType::Interceptor) == 12;
    const std::string tooFar = w.orderInterceptor(*h, away);
    const std::string order = w.orderInterceptor(*h, far);
    check(noneByThemselves && !tooFar.empty() && order.empty() && !w.shots.empty() && w.shots.back().target == far,
          "interceptors: the pilot's order at his target, not the nearest", float(w.shots.empty() ? -1 : w.shots.back().target));
  }

  {
    // The interceptor rack spans the outer pylons too: pods there are left off.
    sim::Weapons w(terrain);
    sim::Loadout lo;
    lo.outer = sim::Store::B8V20;
    lo.inner = sim::Store::Interceptor;
    w.load(lo);
    check(w.loadout().outer == sim::Store::None && w.rounds(sim::WeaponType::S8) == 0 && w.rounds(sim::WeaponType::Interceptor) == 12,
          "interceptors: their rack leaves the outer pylons empty", float(w.rounds(sim::WeaponType::Interceptor)));
  }
  {
    // A player's interceptor goes off ahead of the helicopter, not back or along the door gun,
    // hovering or flying, whichever way the nose points, and peels off a little outwards.
    bool ahead = true;
    float worst = 1.0f;
    for (float heading : {0.0f, 1.3f, -2.4f})
      for (float ms : {0.0f, 35.0f})
        for (int owner : {2, 3}) {
          auto h = std::make_unique<Helicopter>(terrain);
          h->resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 150.0f, 0.0f}, heading, ms);
          sim::Weapons w(terrain);
          sim::Loadout lo;
          lo.inner = sim::Store::Interceptor;
          w.load(lo);
          std::vector<sim::Impact> imp;
          ahead = ahead && w.launchInterceptor(*h, owner);
          for (int i = 0; i < 300 && ahead; ++i) {
            w.step(*h, false, 0.01f, imp);
            const sim::Projectile* d = w.interceptorOf(owner);
            if (!d) { ahead = false; break; }
            const glm::vec3 fwd = h->forward();
            const glm::vec2 f2 = glm::normalize(glm::vec2(fwd.x, fwd.z)), v2 = glm::normalize(glm::vec2(d->vel.x, d->vel.z));
            worst = std::min(worst, glm::dot(f2, v2));
          }
        }
    check(ahead && worst > 0.85f, "interceptors: a player's goes off ahead and peels away gently (worst cos)", worst);
  }
  {
    // A player flies his interceptor out towards a Shahed crossing ahead; he cannot lock it on
    // from afar, but within kInterceptorLock he can, and then it flies into it by itself, his
    // hands free (no drone of his to fly, still one to watch).
    sim::Targets units(terrain);
    auto h = std::make_unique<Helicopter>(terrain);
    h->resetAirborne({0.0f, terrain.groundHeight(0.0f, -1000.0f) + 150.0f, -1000.0f}, 0.0f, 30.0f);
    sim::Weapons w(terrain);
    sim::Loadout lo;
    lo.inner = sim::Store::Interceptor;
    w.load(lo);
    w.doorGunnersFree = false;
    const int shahed = units.add(sim::VehicleKind::Shahed, -2000.0f, -4000.0f, glm::radians(90.0f));
    units.setRoute(shahed, {{{-2000.0f, -4000.0f}, 150.0f, 50.0f}, {{6000.0f, -4000.0f}, 150.0f, 50.0f}}, false, 0.0f);
    std::vector<sim::Impact> imp;
    const bool launched = w.launchInterceptor(*h, 2);
    const bool farRefused = !w.lockInterceptor(2, units, shahed);
    float lockedAt = -1.0f, t = 0.0f;
    bool handsFree = false;
    for (; t < 90.0f && !units.list[size_t(shahed)].destroyed; t += 0.01f) {
      units.step(0.01f);
      w.step(*h, false, 0.01f, imp, &units);
      const sim::Projectile* d = w.interceptorOf(2);
      if (lockedAt < 0.0f && d && glm::distance(d->pos, units.list[size_t(shahed)].pos) < sim::Weapons::kInterceptorLock - 10.0f &&
          w.lockInterceptor(2, units, shahed)) {
        lockedAt = glm::distance(d->pos, units.list[size_t(shahed)].pos);
        handsFree = !w.interceptorOf(2) && w.interceptorWatched(2) && w.interceptorWatched(2)->locked;
      }
    }
    check(launched && farRefused && lockedAt > 0.0f && handsFree && units.list[size_t(shahed)].destroyed,
          "interceptors: a player's locked on a Shahed within a kilometre flies into it by itself (s)", t);
  }
  {
    // The same climbing freely on 90 % collective (stable), one launched after another, each let
    // go to the crew after a few seconds.
    auto h = std::make_unique<Helicopter>(terrain);
    h->model = FlightModel::Stable;
    h->resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 100.0f, 0.0f}, 0.4f, 0.0f);
    sim::Weapons w(terrain);
    sim::Loadout lo;
    lo.inner = sim::Store::Interceptor;
    w.load(lo);
    std::vector<sim::Impact> imp;
    sim::Controls c;
    c.collective = 0.9f;
    float worst = 1.0f;
    int launched = 0;
    for (int i = 0; i < int(60.0f / kDt); ++i) {
      h->step(c, kDt);
      if (i % int(6.0f / kDt) == int(1.0f / kDt)) {
        w.releaseInterceptor(2);
        launched += w.launchInterceptor(*h, 2);
      }
      w.step(*h, false, kDt, imp);
      if (const sim::Projectile* d = w.interceptorOf(2); d && d->age < 4.0f) {
        const glm::vec3 rel = d->vel - h->vel, fwd = h->forward();
        const glm::vec2 f2 = glm::normalize(glm::vec2(fwd.x, fwd.z));
        worst = std::min(worst, glm::dot(f2, glm::normalize(glm::vec2(d->vel.x, d->vel.z))));
        worst = std::min(worst, glm::dot(f2, glm::normalize(glm::vec2(rel.x, rel.z))));
      }
    }
    check(launched >= 8 && worst > 0.85f, "interceptors: launched one by one climbing on 90 % go off ahead (worst cos)", worst);
  }

  {
    // A jet the mission sends along on 200 km/h flies at 450 km/h at least, and still gets to
    // its waypoints.
    sim::Targets units(terrain);
    const sim::VehicleKind su25 = sim::registerVehicleType({"SU-25", "Su_25_Frogfoot", sim::Armour::Light, 12.0f, {-7.0f, 0, -8.0f}, {7.0f, 4.0f, 8.0f}, sim::UnitClass::Aircraft});
    const int j = units.add(su25, 0.0f, -2000.0f, glm::radians(90.0f));
    units.list[size_t(j)].pos.y = terrain.groundHeight(0.0f, -2000.0f) + 60.0f;
    units.setRoute(j, {{{0.0f, -2000.0f}, 60.0f, 56.0f}, {{6000.0f, -2000.0f}, 60.0f, 56.0f}, {{6000.0f, 2000.0f}, 60.0f, 56.0f}},
                   false, 0.0f);
    const glm::vec3 from = units.list[size_t(j)].pos;
    units.step(1.0f);
    const float kmh = glm::distance(glm::vec2(from.x, from.z), glm::vec2(units.list[size_t(j)].pos.x, units.list[size_t(j)].pos.z)) * 3.6f;
    for (int i = 0; i < 120 * 10; ++i) units.step(0.1f);
    check(kmh >= 449.0f && !units.list[size_t(j)].moving(), "jets: a Su-25 at 450 km/h, not the mission's 200, round its route (km/h)", kmh);
  }

  {
    // The gun's ammunition again after a minute sat on the ground, once a flight; not with a
    // full gun (waiting on the pad before take-off does not use it up).
    auto h = std::make_unique<Helicopter>(terrain);
    h->resetOnGround(terrain.airfield().apronCenter, 0.0f);
    sim::Weapons w(terrain);
    w.load(sim::Loadout{});
    std::vector<sim::Impact> imp;
    auto sit = [&](float s) {
      for (int i = 0; i < int(s / kDt); ++i) h->step({}, kDt), w.groundReload(*h, kDt);
    };
    sit(70.0f);
    const bool keptForLater = w.reloads() == 0;
    w.select(sim::WeaponType::YakB);
    const glm::vec2 apron = terrain.airfield().apronCenter;
    h->resetAirborne({apron.x, terrain.groundHeight(apron.x, apron.y) + 100.0f, apron.y}, 0.0f, 0.0f);
    for (int i = 0; i < 240; ++i) w.step(*h, true, kDt, imp);
    h->resetOnGround(apron, 0.0f);
    const int fired = sim::Weapons::kYakBRounds - w.rounds(sim::WeaponType::YakB);
    sit(30.0f);
    const bool notYet = w.reloads() == 0;
    sit(31.0f);
    const bool full = w.reloads() == 1 && w.rounds(sim::WeaponType::YakB) == sim::Weapons::kYakBRounds;
    h->resetAirborne({apron.x, terrain.groundHeight(apron.x, apron.y) + 100.0f, apron.y}, 0.0f, 0.0f);
    for (int i = 0; i < 240; ++i) w.step(*h, true, kDt, imp);
    h->resetOnGround(apron, 0.0f);
    sit(70.0f);
    check(keptForLater && fired > 0 && notYet && full && w.reloads() == 1 && w.rounds(sim::WeaponType::YakB) < sim::Weapons::kYakBRounds,
          "gun reloaded after a minute on the ground, once (rounds fired)", fired);
    {
      // The door guns the same: no more rounds in the air, full after a minute on the ground, once.
      sim::Loadout lo;
      lo.doorGun[1] = true;
      sim::Weapons d(terrain);
      d.load(lo);
      sim::Targets noUnits(terrain);
      const auto burst = [&]() {
        h->resetAirborne({apron.x, terrain.groundHeight(apron.x, apron.y) + 100.0f, apron.y}, 0.0f, 0.0f);
        d.setDoorGunner(1, glm::vec3(1.0f, 0.0f, 0.0f), true);
        for (int i = 0; i < 120; ++i) d.step(*h, false, kDt, imp, &noUnits);
        d.clearDoorGunners();
        h->resetOnGround(apron, 0.0f);
      };
      const auto wait = [&](float s) {
        for (int i = 0; i < int(s / kDt); ++i) h->step({}, kDt), d.groundReload(*h, kDt);
      };
      burst();
      const int left = d.doorRounds(1);
      wait(30.0f);
      const bool still = d.doorRounds(1) == left;
      wait(31.0f);
      const bool loaded = d.doorRounds(1) == sim::kDoorGunRounds && !d.doorReloading(1);
      burst();
      const int second = d.doorRounds(1);
      wait(70.0f);
      check(left < sim::kDoorGunRounds && still && loaded && second < sim::kDoorGunRounds && d.doorRounds(1) == second, "door gun: reloaded by a minute on the ground, once; never in the air", left);
    }
  }

  {
    // A crash at 200 km/h: the pieces fly apart along the way it went, tumble and come to rest
    // on the ground within half a minute, none under it.
    sim::Debris d;
    const float g0 = terrain.groundHeight(0.0f, -2000.0f);
    const std::vector<glm::vec3> centres{{0, 0, 0}, {0, 0.5f, -4}, {0, 0.5f, 4}, {-2, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 1, 8}};
    d.start({0.0f, g0 + 2.0f, -2000.0f}, glm::quat(1, 0, 0, 0), {0.0f, -8.0f, -55.0f}, centres, std::vector<float>(7, 0.6f));
    for (int k = 0; k < 30 * 60; ++k) d.step(terrain, 1.0f / 60.0f);
    float far = 0.0f, near = 1e9f, below = 0.0f;
    bool resting = true;
    for (const sim::DebrisPiece& p : d.pieces()) {
      const float went = -(p.pos.z + 2000.0f);
      far = std::max(far, went), near = std::min(near, went);
      below = std::max(below, terrain.groundHeight(p.pos.x, p.pos.z) - p.pos.y);
      resting &= p.resting;
    }
    check(resting && far > 40.0f && far - near > 15.0f && below <= 0.0f, "crash: the pieces fly apart and come to rest (m)", far);
  }

  // The WSO names what he has by the original's object names.
  check(gamedata::wsoTypeLine("BMP2_APC", true) == gamedata::snd::WsoTypeFirst + 8, "WSO: BMP-2 APC", 0);
  check(gamedata::wsoTypeLine("Hangar_Russian_Large", true) == 236, "WSO: a hangar by the start of its name", 0);
  check(gamedata::wsoTypeLine("Russian_Soldier_AK_47", true) == 287 &&
            gamedata::wsoTypeLine("Russian_Soldier_AK_47", false) == 288,
        "WSO: an enemy in Russian kit is a Kazakh rebel", 0);
  check(gamedata::wsoTypeLine("Car", true) == 256 && gamedata::wsoTypeLine("Caravanserai_Afghan", true) == -1,
        "WSO: whole names match whole", 0);


  {
    // The network game: a snapshot of the pilot's flight puts the gunner's game in the same
    // state, units and shots included, and only what changed goes again.
    sim::Helicopter h1(terrain), h2(terrain);
    sim::Weapons w1(terrain), w2(terrain);
    sim::Targets t1(terrain), t2(terrain);
    sim::EnemyFire e1(terrain), e2(terrain);
    sim::Troops tr1, tr2;
    sim::Wso wso1(terrain), wso2(terrain);
    sim::Spotter sp1(terrain), sp2(terrain);
    sim::MissionState m1, m2;
    t1.placeExercise(), t2.placeExercise();
    sim::Loadout l;
    w1.load(l), w2.load(l);
    h1.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 60.0f, 0.0f}, 0.3f, 40.0f);
    w1.select(sim::WeaponType::S8);
    std::vector<sim::Impact> imp;
    for (int k = 0; k < 240; ++k) {
      h1.step({}, kDt);
      w1.step(h1, true, kDt, imp, &t1);
      t1.step(kDt);
    }
    t1.list[0].destroyed = true;
    net::World a{h1, w1, t1, e1, tr1, wso1, sp1, m1}, b{h2, w2, t2, e2, tr2, wso2, sp2, m2};
    net::Frame fa, fb;
    fa.simTime = 1.0, fa.page = {"DEBRIEFING"};
    fa.wingShots = w1.shots;  // (the AI Hinds' rounds: any will do)
    fa.wingStores.resize(1);
    fa.wingStores[0].index = 3, fa.wingStores[0].store[0] = uint8_t(sim::Store::Interceptor), fa.wingStores[0].rounds[0][1] = 5;
    net::Events ea, eb;
    ea.fired = {sim::WeaponType::S8};
    net::Mirror mirror;
    net::Writer wr;
    mirror.write(wr, 7, a, fa, ea);
    net::Reader rd(wr.bytes);
    uint32_t gen = 0;
    const bool ok = net::Mirror::read(rd, gen, b, fb, eb);
    float worst = 0.0f;
    for (size_t i = 0; i < t1.list.size(); ++i) worst = std::max(worst, glm::distance(t1.list[i].pos, t2.list[i].pos));
    check(ok && gen == 7 && glm::distance(h1.pos, h2.pos) < 1e-4f && w2.shots.size() == w1.shots.size() && !w1.shots.empty() &&
              w2.rounds(sim::WeaponType::S8) == w1.rounds(sim::WeaponType::S8) && t2.list[0].destroyed && worst < 1e-4f &&
              fb.page.size() == 1 && eb.fired.size() == 1 && fb.wingShots.size() == w1.shots.size() && fb.wingStores.size() == 1 &&
              fb.wingStores[0].index == 3 && fb.wingStores[0].store[0] == uint8_t(sim::Store::Interceptor) && fb.wingStores[0].rounds[0][1] == 5,
          "network: a snapshot copies the flight (bytes)", double(wr.bytes.size()));
    net::Writer again;
    mirror.write(again, 7, a, fa, ea);
    check(again.bytes.size() + 40 * t1.list.size() <= wr.bytes.size(), "network: unchanged units are not sent again (bytes)", double(again.bytes.size()));
    net::Reader cut(wr.bytes.data(), wr.bytes.size() / 2);
    check(!net::Mirror::read(cut, gen, b, fb, eb), "network: a cut snapshot is refused", 0);

    // Over the loopback, in pieces as TCP likes.
    net::Listener listener;
    std::string error;
    net::Link server, client;
    bool through = listener.listen(47024, error) && client.connect("127.0.0.1:47024");
    for (int k = 0; k < 200 && through && !server.open(); ++k) listener.accept(server), client.connected();
    std::vector<uint8_t> big(300000, 0x5a), got;
    uint8_t type = 0;
    if (server.open()) client.send(net::Snapshot, big);
    bool received = false;
    for (int k = 0; k < 2000 && server.open() && !received; ++k) client.flush(), received = server.receive(type, got);
    check(received && type == net::Snapshot && got == big, "network: a message gets through the loopback (bytes)", double(got.size()));
  }


  {
    // The network game's gunner: the turret gun is his, fired with his own trigger while the
    // pilot ripples rockets; the pilot's trigger no longer fires the gun.
    sim::Helicopter h(terrain);
    sim::Weapons w(terrain);
    w.load(sim::Loadout{});
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 80.0f, 0.0f}, 0.0f, 40.0f);
    w.select(sim::WeaponType::YakB);
    w.setGunner(true, true, sim::WeaponType::YakB, false);
    std::vector<sim::Impact> imp;
    for (int k = 0; k < 120; ++k) w.step(h, true, kDt, imp);
    const int pilotGun = sim::Weapons::kYakBRounds - w.rounds(sim::WeaponType::YakB);
    w.selectNext();
    const bool rockets = w.armed() && w.selected() == sim::WeaponType::S8;
    const int s8 = w.rounds(sim::WeaponType::S8);
    w.setGunner(true, true, sim::WeaponType::YakB, true);
    for (int k = 0; k < 120; ++k) w.step(h, true, kDt, imp);
    check(pilotGun == 0 && rockets && w.rounds(sim::WeaponType::S8) < s8 && w.rounds(sim::WeaponType::YakB) < sim::Weapons::kYakBRounds,
          "network: gunner fires the gun, pilot the rockets, together", double(sim::Weapons::kYakBRounds - w.rounds(sim::WeaponType::YakB)));
  }

  {
    // ... and the stores too: his E goes round the gun, the 9M114s and the rockets (not the
    // interceptors), his trigger ripples the rockets while the pilot is in navigation mode; at
    // the sight he sends an interceptor out and flies it (owner 1).
    sim::Helicopter h(terrain);
    sim::Weapons w(terrain), wi(terrain);
    w.load(sim::Loadout{});
    sim::Loadout lo;
    lo.inner = sim::Store::Interceptor;
    wi.load(lo);
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 80.0f, 0.0f}, 0.0f, 40.0f);
    const std::vector<sim::WeaponType> ch = w.gunnerChoices(), chi = wi.gunnerChoices();
    const bool choices = std::find(ch.begin(), ch.end(), sim::WeaponType::S8) != ch.end() && ch.front() == sim::WeaponType::YakB &&
                         std::find(chi.begin(), chi.end(), sim::WeaponType::Interceptor) == chi.end();
    const int s8 = w.rounds(sim::WeaponType::S8);
    w.setGunner(true, true, sim::WeaponType::S8, true);
    std::vector<sim::Impact> imp;
    for (int k = 0; k < 120; ++k) w.step(h, false, kDt, imp);
    const bool rockets = w.rounds(sim::WeaponType::S8) < s8;
    wi.setGunner(true, true, sim::WeaponType::Interceptor, true);
    for (int k = 0; k < 60; ++k) wi.step(h, false, kDt, imp);
    const bool noInterceptorTrigger = wi.rounds(sim::WeaponType::Interceptor) == 12;
    const bool drone = wi.launchInterceptor(h, 1) && wi.interceptorOf(1) && !wi.interceptorOf(2) && !wi.launchInterceptor(h, 1);
    check(choices && rockets && noInterceptorTrigger && drone, "network: gunner chooses and fires the stores, flies an interceptor from the sight",
          double(s8 - w.rounds(sim::WeaponType::S8)));
  }


  {
    // The network game's gunner at the right door gun: it fires where he aims it out of the
    // window, not through the airframe; the left one is no business of his.
    sim::Helicopter h(terrain);
    sim::Weapons w(terrain);
    sim::Targets t(terrain);
    sim::Loadout l;
    l.doorGun[0] = l.doorGun[1] = true;
    w.load(l);
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 80.0f, 0.0f}, 0.0f, 40.0f);
    std::vector<sim::Impact> imp;
    w.setDoorGunner(1, glm::normalize(glm::vec3(1.0f, -0.3f, -0.2f)), true);
    for (int k = 0; k < 120; ++k) w.step(h, false, kDt, imp, &t);
    const int out = sim::kDoorGunRounds - w.doorRounds(1);
    w.setDoorGunner(1, glm::vec3(-1.0f, 0.0f, 0.0f), true);
    for (int k = 0; k < 120; ++k) w.step(h, false, kDt, imp, &t);
    const int through = sim::kDoorGunRounds - w.doorRounds(1) - out;
    check(out > 20 && through == 0 && w.doorRounds(0) == sim::kDoorGunRounds,
          "network: the door gunner fires out of his window only", double(out));
  }


  {
    // The planned route changed: the wingman and the Hip (":Follow Player") fly it, from their
    // own places and at their own height and speed, the Hip round the patrol line as often as before.
    gamedata::Mission m;
    m.tasks.emplace_back();
    m.tasks[0].user = true;
    auto wpt = [](float x, float y, float alt, float speed) {
      gamedata::MissionWaypoint w;
      w.pos = {x, y}, w.alt = alt, w.speed = speed;
      return w;
    };
    gamedata::MissionDrone player, hip;
    player.route = {wpt(0, 0, 0, 0), wpt(1000, 5000, 150, 55), wpt(3000, 5000, 150, 55), wpt(0, 0, 0, 55)};
    hip.follow = true;
    hip.route = {wpt(-40, 0, 0, 0), wpt(1000, 5000, 300, 45), wpt(3000, 5000, 300, 45), wpt(1000, 5000, 300, 45), wpt(-40, 0, 0, 45)};
    m.tasks[0].drones = {player, hip};
    m.player()->route[1].pos = {2000.0f, 9000.0f};  // moved on the map
    gamedata::followPlayerRoute(m);
    const auto& r = m.tasks[0].drones[1].route;
    check(r.size() == 5 && r[0].pos == glm::vec2(-40, 0) && r[1].pos == glm::vec2(2000, 9000) && r[2].pos == glm::vec2(3000, 5000) &&
              r[3].pos == glm::vec2(2000, 9000) && r[1].alt == 300.0f && r[1].speed == 45.0f && r[4].pos == glm::vec2(-40, 0) &&
              std::fabs(r[1].time - glm::distance(glm::vec2(-40, 0), glm::vec2(2000, 9000)) / 45.0f) < 0.01f,
          "mission: the flight follows the player's planned route", r.size());
  }


  {
    // Battle damage knocking systems out (the manual's failure warning lights): a failed gun does
    // not fire, a failed undercarriage stays where it was.
    sim::Helicopter h(terrain);
    sim::Weapons w(terrain);
    w.load(sim::Loadout{});
    h.resetAirborne({0.0f, terrain.groundHeight(0.0f, 0.0f) + 80.0f, 0.0f}, 0.0f, 40.0f);
    h.fail(sim::System::Gun);
    h.fail(sim::System::Gear);
    w.select(sim::WeaponType::YakB);
    std::vector<sim::Impact> imp;
    for (int k = 0; k < 120; ++k) w.step(h, true, kDt, imp), h.step({0.0f, 0.0f, 0.0f, 0.78f}, kDt);
    const float gear = h.gearPos;
    h.toggleGear();
    for (int k = 0; k < 480; ++k) h.step({0.0f, 0.0f, 0.0f, 0.78f}, kDt);
    check(w.rounds(sim::WeaponType::YakB) == sim::Weapons::kYakBRounds && h.gearPos == gear && !h.works(sim::System::Gun),
          "damage: a failed gun is silent, a failed undercarriage stuck", h.gearPos);
  }


  {
    // Mid-air collisions: flying through a helicopter (its blades reach past its box) or into a
    // drone; one passing 30 m off, or a helicopter standing on the ground, is no collision.
    world::Terrain flat;
    sim::Targets t(flat);
    const sim::VehicleKind kind = sim::registerVehicleType({"MI-24", "Mi_24_Collide", sim::Armour::Light, 12.0f, {-1.0f, 0, -9}, {1.0f, 4, 9},
                                                            sim::UnitClass::Helicopter});
    const int air = t.add(kind, 0.0f, 0.0f, 0.0f), ground = t.add(kind, 500.0f, 0.0f, 0.0f);
    t.list[size_t(air)].pos.y += 150.0f;
    auto at = [&](glm::vec3 p) {
      std::vector<glm::vec3> pts;
      for (const glm::vec3& b : sim::Helicopter::outline()) pts.push_back(p + b);
      return t.airCollision(pts);
    };
    const glm::vec3 a = t.list[size_t(air)].pos, g = t.list[size_t(ground)].pos;
    check(at(a + glm::vec3(10.0f, 0.0f, 0.0f)) == air && at(a + glm::vec3(30.0f, 0.0f, 0.0f)) < 0 && at(g + glm::vec3(0.0f, 1.0f, 0.0f)) < 0,
          "collision: through a helicopter in the air, not past it", 0);
  }


  {
    // The door gunner's shot at a drone (doorShot): good only close by, its speed matched and from
    // above; he holds his fire at one crossing fast or level with him, and waits while closing in.
    using W = sim::Weapons;
    const float alongside = W::doorShot({0.0f, -30.0f, 100.0f}, {0.0f, 0.0f, 0.0f});
    const float far = W::doorShot({0.0f, -30.0f, 500.0f}, {0.0f, 0.0f, 0.0f});
    const float crossing = W::doorShot({0.0f, -30.0f, 100.0f}, {50.0f, 0.0f, 0.0f});
    const float level = W::doorShot({0.0f, 0.0f, 100.0f}, {0.0f, 0.0f, 0.0f});
    check(alongside > 0.9f && far == 0.0f && crossing < W::kDoorOpenFire && level < W::kDoorOpenFire,
          "drones: door gunner fires only alongside, close and from above", alongside);
  }

  {
    // The Pilot's Log: ranks by the missions flown, medals for campaigns and singles won, a crash
    // putting a pilot out of action (not Comrade Immortal), the log saved and read back.
    sim::PilotLog log;
    log.reset();
    check(log.create("Ivanov") && log.current == 1 && sim::rankFor(4) == sim::Rank::Sergeant && sim::rankFor(5) == sim::Rank::Lieutenant &&
              sim::rankFor(50) == sim::Rank::Colonel,
          "log: a new pilot, ranks at 5 and 50 missions", 0);
    sim::FlightRecord win;
    win.airborneSeconds = 600.0, win.result = 1, win.zone = "afghan";
    int promoted = -1;
    for (int i = 0; i < 5; ++i) promoted = std::max(promoted, log.record(win, 1000).promotedTo);
    check(promoted == int(sim::Rank::Lieutenant) && log.pilot().successful == 5 && log.pilot().singlesWon == 5 &&
              std::fabs(log.pilot().totalSeconds - 3000.0) < 1.0,
          "log: five missions won make a lieutenant", promoted);
    sim::FlightRecord camp = win;
    camp.campaign = true, camp.campaignWon = true;
    for (int i = 0; i < 5; ++i) log.record(win, 1000);  // ten singles won
    const sim::LogOutcome o = log.record(camp, 1000);
    check((o.newMedals & sim::kRedStar) && !(log.pilot().medals & sim::kLenin), "log: a campaign and ten singles: the Order of the Red Star", int(o.newMedals));
    sim::FlightRecord crash = win;
    crash.crashed = true;
    const sim::LogOutcome hurt = log.record(crash, 1000);
    check(hurt.status == sim::PilotStatus::Injured && !log.canLog(1000 + 3600) && log.canLog(1000 + 25 * 3600),
          "log: a crash at home: injured for a day", int(hurt.status));
    crash.died = true;
    log.refresh(1000 + 25 * 3600);
    log.record(crash, 1000 + 25 * 3600);
    const bool dead = log.pilot().status == sim::PilotStatus::Kia && !log.record(win, 2000000).logged;
    log.current = 0;
    log.record(crash, 2000000);
    check(dead && log.pilot().status == sim::PilotStatus::Active, "log: killed in action logs no more; Comrade Immortal lives", 0);
    const std::string path = "/tmp/hind_test_pilotlog.txt";
    {
      // The mission list's x: a mission whose last logged flight failed.
      log.current = 0;  // (Comrade Immortal: the other one is out of action by now)
      sim::FlightRecord lost, again;
      lost.mission = "KOREAS06", lost.result = 0;
      again.mission = "KOREAS05", again.result = 0;
      log.record(lost, 0), log.record(again, 0);
      again.result = 1;
      log.record(again, 0);
    }
    log.save(path);
    sim::PilotLog back;
    check(back.load(path) && back.pilots.size() == 2 && back.pilots[1].name == "Ivanov" && back.pilots[1].medals == log.pilots[1].medals &&
              back.pilots[1].campaignsWon.size() == 1 && back.pilots[0].immortal,
          "log: saved and read back", int(back.pilots.size()));
    check(back.pilots[0].failedMissions == std::vector<std::string>{"KOREAS06"}, "log: failed missions kept, one won since gone", int(back.pilots[0].failedMissions.size()));
    std::remove(path.c_str());
  }

  {
    // The campaign as the original's: a success up a level, a failure a life (and that mission
    // marked), the last life the campaign's end; the state kept in hind.ini's keys.
    gamedata::CampaignState c;
    c.mission = "AFGHNC01";
    gamedata::campaignResult(c, false, 10);
    const bool stalemate = c.show == gamedata::CampaignShow::Stalemate && c.lives == 2 && c.failed.size() == 1 && c.level == 1;
    c.mission = "AFGHNC02";
    gamedata::campaignResult(c, true, 10);
    const bool up = c.level == 2 && c.failed.empty() && c.show == gamedata::CampaignShow::Situation && c.lives == 2;
    platform::Config cfg;
    gamedata::saveCampaign(cfg, "afghan", c);
    const gamedata::CampaignState back = gamedata::loadCampaign(cfg, "afghan", "");
    c.mission = "AFGHNC04";
    gamedata::campaignResult(c, false, 10);
    gamedata::campaignResult(c, false, 10);
    const bool lost = c.show == gamedata::CampaignShow::Failure && c.lives == 0;
    c = {};
    c.level = 10;
    gamedata::campaignResult(c, true, 10);
    check(stalemate && up && back.level == 2 && back.lives == 2 && lost && c.show == gamedata::CampaignShow::Won,
          "campaign: stalemate, up a level, saved, lost, won", c.level);
  }

  {
    // The Top Ten: a score's place, the eleventh dropping off, saved and read back.
    sim::TopTen tt;
    for (int i = 1; i <= 10; ++i) tt.insert("P" + std::to_string(i), i * 100);
    const int low = tt.place(50), mid = tt.place(550);
    tt.insert("New", 550);
    const std::string path = "/tmp/hind_test_topten.txt";
    tt.save(path);
    sim::TopTen back;
    back.load(path);
    std::remove(path.c_str());
    {
      // The crash mark goes with a score, and a line without it is read as before.
      sim::TopTen crash;
      crash.insert("Ivan", 900, true), crash.insert("Olga", 700);
      crash.save(path);
      sim::TopTen read;
      read.load(path);
      std::remove(path.c_str());
      check(read.entries.size() == 2 && read.entries[0].crashed && read.entries[0].name == "Ivan" && !read.entries[1].crashed && read.entries[1].name == "Olga",
            "top ten: the crash mark saved and read back", int(read.entries.size()));
    }
    check(low == -1 && mid == 5 && back.entries.size() == 10 && back.entries[5].name == "New" && back.entries.back().score == 200 &&
              back.entries.front().score == 1000,
          "top ten: place, insert, saved and read back", mid);
  }

  {
    // An animation (ANIMS/*.Q3): a first frame as it is, then a change of it in 8x8 blocks, one
    // filled with a colour, one with a pixel of its second row changed. With the CD: all of FLY.
    std::vector<uint8_t> d = {'A', 'N', 'I', 'M', 2, 0, 16, 0, 8, 0, 0, 1};
    auto u32 = [&](uint32_t v) {
      for (int k = 0; k < 4; ++k) d.push_back(uint8_t(v >> (8 * k)));
    };
    d.push_back(0x01);
    u32(768 + 128);
    for (int i = 0; i < 256; ++i) d.insert(d.end(), {uint8_t(i / 4), uint8_t(i / 4), uint8_t(i / 4)});
    d.insert(d.end(), 128, 1);
    const std::vector<uint8_t> change = {0, 0, 200, 6, 0x02, 0x01, 77};  // (palette change), fill 200; row 1: pixel 0
    d.push_back(0x30);
    u32(uint32_t(change.size()));
    u32(1);
    d.insert(d.end(), change.begin(), change.end());
    gamedata::Anim a;
    std::string error;
    std::vector<uint8_t> rgb;
    const bool opened = a.open(d, error);
    const bool first = a.next() && a.next();
    a.rgb(rgb);
    auto at = [&](int x, int y) { return int(rgb[size_t(y * 16 + x) * 3]); };
    bool decoded = opened && first && !a.next() && at(3, 5) == 200 / 4 * 4 + 200 / 4 / 16 && at(8, 1) == 77 / 4 * 4 + 77 / 4 / 16 &&
                   at(9, 1) == 0 && at(8, 0) == 0;
    int frames = -1;
    gamedata::Cd cd;
    std::vector<uint8_t> fly;
    if (cd.open("", gamedata::findOriginalDir("")) && cd.read("ANIMS/FLY.Q3", fly) && a.open(fly, error)) {
      for (frames = 0; a.next();) ++frames;
      decoded = decoded && frames == 250;
    }
    check(decoded, "animation: first frame, blocks changed (FLY.Q3 from the CD if there)", frames);
  }

  {
    // Orders to the wingman (manual p. 28): he follows off the player's right, attacks the
    // player's target (a Shahed alongside), goes home and lands; with no wingman nobody answers.
    world::Terrain flat;
    sim::Targets t(flat);
    const float dt = 1.0f / 60.0f;
    const sim::VehicleKind hind = sim::registerVehicleType({"MI-24", "Mi_24_Hind_Wingman", sim::Armour::Light, 12.0f, {-2, 0, -9}, {2, 4, 9},
                                                            sim::UnitClass::Helicopter});
    const int w = t.add(hind, 0.0f, 0.0f, 0.0f);
    t.list[size_t(w)].side = sim::Side::Ours;
    t.setRoute(w, {{{0.0f, 0.0f}, 0.0f, 1.0f, 30.0f}, {{0.0f, -20000.0f}, 150.0f, 55.0f}}, false, 0.0f);
    auto h = std::make_unique<Helicopter>(flat);
    h->resetAirborne({-800.0f, 150.0f, 0.0f}, glm::radians(90.0f), 50.0f);
    sim::Orders o;
    std::string said;
    std::vector<sim::Impact> im;
    auto fly = [&](float seconds) {
      for (float s2 = 0.0f; s2 < seconds; s2 += dt) {
        h->pos += h->vel * dt;
        t.step(dt);
        o.step(*h, t, flat, dt, im);
      }
    };
    const int follow = o.give(sim::Order::Follow, false, *h, t, -1, said);
    fly(90.0f);
    const glm::vec3 f = h->forward();
    const glm::vec2 fwd = glm::normalize(glm::vec2(f.x, f.z)), right{-fwd.y, fwd.x};
    const glm::vec2 slot = glm::vec2(h->pos.x, h->pos.z) + right * 60.0f - fwd * 40.0f;
    const float off = glm::distance(glm::vec2(t.list[size_t(w)].pos.x, t.list[size_t(w)].pos.z), slot);
    check(follow == 67 && off < 250.0f, "orders: the wingman follows off our right (m from his place)", off);
    const int drone = t.add(sim::VehicleKind::Shahed, h->pos.x + 600.0f, h->pos.z + 400.0f, glm::radians(90.0f));
    t.setRoute(drone, {{{h->pos.x + 600.0f, h->pos.z + 400.0f}, 150.0f, 50.0f}, {{h->pos.x + 30000.0f, h->pos.z + 400.0f}, 150.0f, 50.0f}}, false, 0.0f);
    const int attack = o.give(sim::Order::Attack, false, *h, t, drone, said);
    float sec = 0.0f;
    for (; sec < 180.0f && !t.list[size_t(drone)].destroyed; sec += 1.0f) fly(1.0f);
    check(attack == 68 && t.list[size_t(drone)].destroyed, "orders: the wingman downs our target (s)", sec);
    const int home = o.give(sim::Order::Home, false, *h, t, -1, said);
    fly(400.0f);
    const sim::Vehicle& wv = t.list[size_t(w)];
    check(home == 69 && glm::length(glm::vec2(wv.pos.x, wv.pos.z)) < 30.0f && wv.pos.y < flat.groundHeight(wv.pos.x, wv.pos.z) + 0.5f && !wv.hover, "orders: home, the wingman lands at the base (m off)",
          glm::length(glm::vec2(wv.pos.x, wv.pos.z)));
    sim::Targets none(flat);
    sim::Orders o2;
    check(o2.give(sim::Order::Follow, false, *h, none, -1, said) < 0 && said == "NO WINGMAN", "orders: no wingman, no answer", 0);
  }

  {
    // The lone bowman: an arrow loosed fully drawn at 45 degrees flies some 700 m; aimed at a Hind
    // hovering 200 m off and 40 m up, the warhead goes off by it; two direct hits bring it down;
    // the next warhead takes its while (kFitTime) to screw on.
    world::Terrain ground;
    const glm::vec2 at = af.center;
    sim::Bowman b;
    b.place(at, 0.0f, ground);
    b.pitch = glm::radians(45.0f);
    glm::vec3 burst{0.0f};
    const float dt = 1.0f / 60.0f;
    bool off = false;
    for (float t = 0.0f; t < sim::Bowman::kDrawTime + 0.2f; t += dt) b.step(true, dt, ground, {}, {}, burst);
    for (float t = 0.0f; t < 20.0f && !off; t += dt) off = b.step(false, dt, ground, {}, {}, burst);
    const float range = glm::length(glm::vec2(burst.x - b.pos.x, burst.z - b.pos.z));
    check(off && range > 550.0f && range < 800.0f, "bowman: a full draw at 45 deg flies (m)", range);
    check(b.state == sim::Bowman::State::Fitting && b.fitting() < 0.01f, "bowman: then the next warhead is screwed on (s)", b.fit);
    {
      // Letting the string down: no arrow goes, and no new draw till the trigger is let go of.
      sim::Bowman d;
      d.place({0.0f, 0.0f}, 0.0f, ground);
      glm::vec3 at;
      for (int k = 0; k < 60; ++k) d.step(true, dt, ground, {}, {}, at);
      const bool drawing = d.state == sim::Bowman::State::Drawing && d.draw > 0.0f;
      d.letDown();
      d.step(true, dt, ground, {}, {}, at);
      const bool held = d.state == sim::Bowman::State::Ready && d.draw == 0.0f;
      d.step(false, dt, ground, {}, {}, at);
      const bool still = d.state == sim::Bowman::State::Ready;
      d.step(true, dt, ground, {}, {}, at);
      check(drawing && held && still && d.state == sim::Bowman::State::Drawing, "bowman: the right button lets the string down without loosing", d.draw);
    }
    for (float t = 0.0f; t < sim::Bowman::kFitTime - 0.1f; t += dt) b.step(true, dt, ground, {}, {}, burst);
    check(b.state == sim::Bowman::State::Fitting, "bowman: still screwing 0.1 s before the end", b.fit);
    for (float t = 0.0f; t < 0.2f; t += dt) b.step(false, dt, ground, {}, {}, burst);
    check(b.state == sim::Bowman::State::Ready, "bowman: ready after kFitTime", b.fit);

    auto hind = std::make_unique<Helicopter>(ground);
    const glm::vec3 hp = b.eye() + glm::vec3(0.0f, 40.0f, -200.0f);
    hind->resetAirborne(hp, 0.0f, 0.0f);
    // Aimed a little high for the drop over 200 m at 85 m/s (about 27 m).
    const glm::vec3 to = hp - b.eye();
    b.yaw = 0.0f, b.pitch = std::atan2(to.y, -to.z) + std::atan2(27.0f, 200.0f);
    for (float t = 0.0f; t < sim::Bowman::kDrawTime + 0.2f; t += dt) b.step(true, dt, ground, {}, {}, burst);
    off = false;
    for (float t = 0.0f; t < 10.0f && !off; t += dt) off = b.step(false, dt, ground, {{hind->pos, hind->orient}}, {}, burst);
    const float miss = sim::hindDistance(burst, {hind->pos, hind->orient});
    check(off && miss < sim::Bowman::kFuse + 0.1f, "bowman: the warhead goes off by the Hind (m off it)", miss);
    sim::arrowBlast(*hind, burst);
    const float after1 = hind->damage.airframe;
    check(!hind->damage.shotDown && after1 < 70.0f, "bowman: one direct hit cripples the Hind (airframe)", after1);
    sim::arrowBlast(*hind, burst);
    check(hind->damage.shotDown && hind->damage.lastHitBy == "A LONE BOWMAN", "bowman: the second brings it down", hind->damage.airframe);
    // Shot down on the ground (or on reaching it) it does not sit there: it crashes.
    auto parked = std::make_unique<Helicopter>(ground);
    parked->resetOnGround(at, 0.0f);
    for (int k = 0; k < 2; ++k) sim::arrowBlast(*parked, parked->pos);
    for (int k = 0; k < 30 && !parked->crashed; ++k) parked->step({}, kDt);
    check(parked->damage.shotDown && parked->crashed, "bowman: a Hind shot down on the ground blows up", parked->damage.airframe);
    // Walking into a parked Hind he stops at its side; past its nose he walks on.
    {
      auto standing = std::make_unique<Helicopter>(ground);
      standing->resetOnGround(at + glm::vec2(0.0f, -12.0f), glm::radians(90.0f));  // across his way
      sim::Bowman w;
      w.place(at, 0.0f, ground);
      for (float t = 0.0f; t < 3.0f; t += dt) w.walk({0.0f, 1.0f}, false, dt, ground, {{standing->pos, standing->orient}});
      const float gap = at.y - w.pos.z;
      check(gap > 8.0f && gap < 11.5f, "bowman: walks up to a parked Hind and stops (m walked)", gap);
      sim::Bowman b2;
      b2.place(at + glm::vec2(14.0f, 0.0f), 0.0f, ground);
      for (float t = 0.0f; t < 4.0f; t += dt) b2.walk({0.0f, 1.0f}, false, dt, ground, {{standing->pos, standing->orient}});
      check(at.y - b2.pos.z > 20.0f, "bowman: past its nose he walks on (m)", at.y - b2.pos.z);
    }
    // Faster than a man: some 30 km/h walking, 100 km/h running, getting going gradually.
    {
      sim::Bowman r;
      r.place(at, 0.0f, ground);
      for (float t = 0.0f; t < 0.3f; t += dt) r.walk({0.0f, 1.0f}, false, dt, ground);
      const float early = glm::length(r.vel);
      check(early > 0.5f && early < 3.0f, "bowman: gets going gradually (m/s after 0.3 s)", early);
      for (float t = 0.0f; t < 3.0f; t += dt) r.walk({0.0f, 1.0f}, false, dt, ground);
      check(std::fabs(glm::length(r.vel) * 3.6f - 30.0f) < 1.0f, "bowman: walks at 30 km/h", glm::length(r.vel) * 3.6f);
      for (float t = 0.0f; t < 4.0f; t += dt) r.walk({0.0f, 1.0f}, true, dt, ground);
      check(std::fabs(glm::length(r.vel) * 3.6f - 100.0f) < 1.0f, "bowman: runs at 100 km/h", glm::length(r.vel) * 3.6f);
      for (float t = 0.0f; t < 2.0f; t += dt) r.walk({0.0f, 0.0f}, false, dt, ground);
      check(glm::length(r.vel) == 0.0f, "bowman: stops when let go", glm::length(r.vel));
    }
    // Where the drawn arrow comes down is where it does.
    {
      sim::Bowman l;
      l.place(at, 0.3f, ground);
      l.pitch = glm::radians(12.0f);
      glm::vec3 shown{0.0f}, went{0.0f};
      bool known = false, offNow = false;
      for (float t = 0.0f; t < 1.2f; t += dt) l.step(true, dt, ground, {}, {}, went);
      known = l.landing(l.draw, ground, shown);
      for (float t = 0.0f; t < 20.0f && !offNow; t += dt) offNow = l.step(false, dt, ground, {}, {}, went);
      const float off = glm::length(glm::vec2(shown.x - went.x, shown.z - went.z));
      check(known && offNow && off < 2.0f, "bowman: the ring is where the arrow comes down (m off)", off);
    }
    // Aimed over a Hind hovering in its way, the ring is where its fuse sets it off by it.
    {
      sim::Bowman l;
      l.place(at, 0.0f, ground);
      auto hover = std::make_unique<Helicopter>(ground);
      hover->resetAirborne(l.eye() + glm::vec3(0.0f, 30.0f, -150.0f), 0.0f, 0.0f);
      const glm::vec3 to = hover->pos - l.eye();
      l.yaw = 0.0f, l.pitch = std::atan2(to.y, -to.z) + std::atan2(15.0f, 150.0f);
      glm::vec3 shown{0.0f};
      bool struck = false;
      const bool known = l.landing(1.0f, ground, shown, nullptr, {{hover->pos, hover->orient}}, {}, &struck);
      const float off = sim::hindDistance(shown, {hover->pos, hover->orient});
      check(known && struck && off < sim::Bowman::kFuse + 0.1f, "bowman: the ring where its fuse sets it off by a Hind on the way (m off it)", off);
      bool clear = true;
      l.landing(1.0f, ground, shown, nullptr, {{hover->pos + glm::vec3(60.0f, 0.0f, 0.0f), hover->orient}}, {}, &clear);
      check(!clear, "bowman: going well by a Hind, not struck", 0.0f);
    }
    // The M60: some 9 rounds a second; a belt of 200, then a few seconds putting in the next; its
    // rounds find a Hind 150 m ahead.
    {
      sim::Bowman g;
      g.place(at, 0.0f, ground);
      g.change();
      const glm::vec3 hp2 = g.eye() + glm::vec3(0.0f, 0.0f, -150.0f);
      auto target = std::make_unique<Helicopter>(ground);
      target->resetAirborne(hp2, glm::radians(90.0f), 0.0f);
      std::vector<sim::Bowman::Hit> hits;
      for (float t = 0.0f; t < 1.0f; t += dt) g.stepM60(true, dt, ground, {{target->pos, target->orient, int8_t(0)}}, {}, hits);
      const int fired = sim::Bowman::kM60Belt - g.belt;
      check(fired >= 8 && fired <= 10, "bowman: the M60 fires some 9 rounds a second", float(fired));
      for (float t = 0.0f; t < 1.0f; t += dt) g.stepM60(false, dt, ground, {{target->pos, target->orient, int8_t(0)}}, {}, hits);
      const long onHind = std::count_if(hits.begin(), hits.end(), [](const sim::Bowman::Hit& h) { return h.hind == 0; });
      check(onHind >= fired - 1, "bowman: its rounds hit the Hind 150 m ahead", float(onHind));
      for (float t = 0.0f; t < 30.0f && g.reload <= 0.0f; t += dt) g.stepM60(true, dt, ground, {}, {}, hits);
      check(g.belt == 0 && g.reload > 0.0f, "bowman: a belt is 200 rounds, then he reloads", float(g.belt));
      for (float t = 0.0f; t < sim::Bowman::kM60Reload + 0.1f; t += dt) g.stepM60(true, dt, ground, {}, {}, hits);
      check(g.belt > sim::Bowman::kM60Belt - 3, "bowman: the next belt in after kM60Reload", float(g.belt));
      g.change();
      const int left = g.belt;
      for (float t = 0.0f; t < 0.5f; t += dt) g.stepM60(true, dt, ground, {}, {}, hits);
      check(g.belt == left, "bowman: with the LAW in his hands the M60 does not fire", float(g.belt));
    }
    // The LAW: held on a Hind 400 m ahead it locks on in kLockTime, not on one 10 degrees aside;
    // fired locked, its rocket gets a Hind crossing at 60 m/s; fired unlocked, it follows his look
    // as he turns; the next tube only after kLawReady.
    {
      sim::Bowman b;
      b.place(at, 0.0f, ground);
      b.change(), b.change();
      check(b.weapon == sim::Bowman::Weapon::Law, "bowman: Enter twice: the LAW", 0.0f);
      const glm::vec3 eye = b.eye();
      std::vector<sim::HindShape> hinds{{eye + glm::vec3(0.0f, 40.0f, -400.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), int8_t(0)}};
      const auto aimAt = [&](glm::vec3 p) {
        const glm::vec3 to = p - b.eye();
        b.yaw = std::atan2(to.x, -to.z), b.pitch = std::atan2(to.y, glm::length(glm::vec2(to.x, to.z)));
      };
      sim::Bowman::Hit burst;
      // Aside: nothing.
      aimAt(hinds[0].pos + glm::vec3(70.0f, 0.0f, 0.0f));
      for (float t = 0.0f; t < 1.0f; t += dt) b.stepLaw(false, dt, ground, hinds, {}, burst);
      check(!b.locked && b.aimHind < 0, "bowman: the LAW does not lock on a Hind 10 degrees aside", float(b.aimHind));
      aimAt(hinds[0].pos);
      float t = 0.0f;
      for (; t < 2.0f && !b.locked; t += dt) b.stepLaw(false, dt, ground, hinds, {}, burst);
      check(b.locked && std::fabs(t - sim::Bowman::kLockTime) < 0.1f, "bowman: the LAW locks on a Hind ahead in kLockTime (s)", t);
      // Fired at it as it starts crossing at 60 m/s (he still looks where it was).
      b.stepLaw(true, dt, ground, hinds, {}, burst);
      check(b.rocket.flying && b.rocket.hind == 0, "bowman: fired locked, the rocket goes for that Hind", float(b.rocket.hind));
      bool hit = false;
      for (t = 0.0f; t < sim::Bowman::kLawLife && !hit; t += dt) {
        hinds[0].pos.x += 60.0f * dt;
        hit = b.stepLaw(false, dt, ground, hinds, {}, burst);
      }
      check(hit && burst.hind == 0, "bowman: the locked rocket gets the Hind crossing at 60 m/s (s)", t);
      check(b.lawReady > 0.0f && !b.stepLaw(true, dt, ground, hinds, {}, burst) && !b.rocket.flying, "bowman: no next tube at once", b.lawReady);
      for (t = 0.0f; t < sim::Bowman::kLawReady; t += dt) b.stepLaw(false, dt, ground, {}, {}, burst);
      // Unlocked: up at the empty sky, turned 25 degrees right after a quarter of a second.
      b.pitch = 0.3f, b.yaw = 0.0f;
      b.stepLaw(true, dt, ground, {}, {}, burst);
      check(b.rocket.flying && b.rocket.hind < 0 && b.rocket.unit < 0, "bowman: fired unlocked, the rocket rides his look", 0.0f);
      float early = 0.0f;  // how far it has turned a quarter of a second after he did (degrees)
      for (t = 0.0f; t < 3.0f && b.rocket.flying; t += dt) {
        if (t > 0.25f) b.yaw = glm::radians(25.0f);
        b.stepLaw(false, dt, ground, {}, {}, burst);
        if (t < 0.5f) early = glm::degrees(std::atan2(b.rocket.vel.x, -b.rocket.vel.z));
      }
      check(early < glm::degrees(sim::Bowman::kLawGuidedTurn * 0.25f) + 0.5f, "bowman: unlocked, the rocket turns after his look slowly (degrees of 25 in 0.25 s)", early);
      const glm::vec3 to = b.rocket.pos - b.eye();
      const float off = glm::length(to - b.look() * glm::dot(to, b.look()));
      check(b.rocket.flying && off < 10.0f, "bowman: unlocked, the rocket is on his look after he turned (m off it)", off);
    }
    // The lock: lost once the Hind is out of the circle (he holding still); locked, the rocket a
    // heat seeker's: the Hind's flares lure about half of them off it from behind, its jammer
    // some third.
    {
      sim::Bowman b;
      b.place(at, 0.0f, ground);
      b.change(), b.change();
      std::vector<sim::HindShape> hinds{{b.eye() + glm::vec3(0.0f, 40.0f, -400.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), int8_t(0)}};
      sim::Bowman::Hit burst;
      const auto lockOn = [&]() {
        const glm::vec3 to = hinds[0].pos - b.eye();
        b.yaw = std::atan2(to.x, -to.z), b.pitch = std::atan2(to.y, glm::length(glm::vec2(to.x, to.z)));
        for (float t = 0.0f; t < 2.0f && !b.locked; t += dt) b.stepLaw(false, dt, ground, hinds, {}, burst);
        return b.locked;
      };
      check(lockOn(), "bowman: the LAW locked for the lock's test", 0.0f);
      const glm::vec3 was = hinds[0].pos;
      hinds[0].pos.x += 400.0f * std::tan(sim::Bowman::kKeepCone * 1.3f);
      b.stepLaw(false, dt, ground, hinds, {}, burst);
      check(!b.locked && b.aimHind < 0, "bowman: the LAW's lock lost once the Hind is out of the circle", float(b.aimHind));
      hinds[0].pos = was;
      // Fired locked, flares let go half a second later; then, no flares, the jammer on.
      const auto fly = [&](bool flare, bool jammer) {
        hinds[0].jammer = jammer;
        b.lawReady = 0.0f;
        b.rocket.flying = false;
        if (!lockOn()) return false;
        b.stepLaw(true, dt, ground, hinds, {}, burst);
        std::vector<sim::Flare> flares;
        for (float t = 0.0f; t < 1.5f && b.rocket.flying; t += dt) {
          if (flare && flares.empty() && t > 0.5f)
            flares = {{hinds[0].pos + glm::vec3(-5.0f, -3.0f, 4.0f), glm::vec3(0.0f), 0.0f, 7},
                      {hinds[0].pos + glm::vec3(5.0f, -3.0f, 4.0f), glm::vec3(0.0f), 0.0f, 8}};
          b.stepLaw(false, dt, ground, hinds, {}, burst, flares);
        }
        return b.rocket.decoyed();
      };
      int lured = 0, jammed = 0, none = 0;
      for (int k = 0; k < 60; ++k) lured += fly(true, false) ? 1 : 0;
      for (int k = 0; k < 60; ++k) jammed += fly(false, true) ? 1 : 0;
      for (int k = 0; k < 20; ++k) none += fly(false, false) ? 1 : 0;
      check(lured > 18 && lured < 42, "bowman: flares lure about half the locked rockets from behind (of 60)", float(lured));
      check(jammed > 10 && jammed < 32, "bowman: the jammer makes about a third of them wander off (of 60)", float(jammed));
      check(none == 0, "bowman: without flares or jammer no rocket is lured off (of 20)", float(none));
    }
    // Hard to kill: a rocket at his feet takes most of his health, one 15 m off nothing; a door
    // gun's round through him 2 of 100; dead, he is back after kDeadTime.
    {
      sim::Bowman h;
      h.place(at, 0.0f, ground);
      check(std::fabs(h.damageBy({sim::WeaponType::S8, h.pos}) - 70.0f) < 0.1f, "bowman: an S-8 at his feet takes 70", h.damageBy({sim::WeaponType::S8, h.pos}));
      check(h.damageBy({sim::WeaponType::S8, h.pos + glm::vec3(15.0f, 0.0f, 0.0f)}) == 0.0f, "bowman: one 15 m off nothing", 0.0f);
      const float through = h.damageBy(sim::WeaponType::DoorGun, h.pos + glm::vec3(-5.0f, 1.2f, 0.0f), h.pos + glm::vec3(5.0f, 1.2f, 0.0f));
      const float by = h.damageBy(sim::WeaponType::DoorGun, h.pos + glm::vec3(-5.0f, 1.2f, 1.0f), h.pos + glm::vec3(5.0f, 1.2f, 1.0f));
      check(through == 2.0f && by == 0.0f, "bowman: a round through him takes 2, a metre aside nothing", through);
      int rounds = 0;
      while (!h.hurt(through)) ++rounds;
      check(rounds >= 45 && h.dead() && h.blood > 0.5f, "bowman: some 50 door gun rounds to kill him", float(rounds + 1));
      bool back = false;
      for (float t = 0.0f; t < sim::Bowman::kDeadTime - 0.1f; t += dt) back = h.stepDead(dt);
      check(!back, "bowman: lies dead a while", 0.0f);
      for (float t = 0.0f; t < 0.2f; t += dt) back = h.stepDead(dt);
      check(back, "bowman: then he may be back", 0.0f);
    }
    auto far = std::make_unique<Helicopter>(ground);
    far->resetAirborne(hp, 0.0f, 0.0f);
    check(!sim::arrowBlast(*far, hp + glm::vec3(0.0f, 0.0f, -30.0f)) && far->damage.airframe == 100.0f, "bowman: 20 m off the nose does nothing", far->damage.airframe);
  }

  // The door gun mount: stop to stop over the gun's whole field.
  {
    namespace door = platform::door;
    const float deg = door::kDegree;
    const door::Aim right = door::aimFromAxes(1.0f, 0.0f), left = door::aimFromAxes(-1.0f, 0.0f);
    check(std::fabs(right.yaw + 69.0f * deg) < 1e-4f && std::fabs(left.yaw - 69.0f * deg) < 1e-4f, "mount: the stops are the gun's limits to the sides (+ left, as Camera::turn)", right.yaw);
    check(std::fabs(door::aimFromAxes(0.0f, -1.0f).pitch + 69.0f * deg) < 1e-4f, "mount: its lower stop is the gun's 69 degrees down", door::aimFromAxes(0.0f, -1.0f).pitch);
    check(std::fabs(door::aimFromAxes(0.0f, 1.0f).pitch - 29.0f * deg) < 1e-4f, "mount: its upper stop is the gun's 29 degrees up", door::aimFromAxes(0.0f, 1.0f).pitch);
    // One degree on the real mount is one in the game, level where the real gun is level.
    const float level = (0.0f + 69.0f) / (69.0f + 29.0f) * 2.0f - 1.0f;
    check(std::fabs(door::aimFromAxes(0.0f, level).pitch) < 1e-4f, "mount: level on the mount is level in the game, not the axis's middle", level);
    check(door::aimFromAxes(0.0f, 0.0f).pitch < -19.0f * deg, "mount: the axis's middle points down", door::aimFromAxes(0.0f, 0.0f).pitch);
    check(door::aimFromAxes(3.0f, -3.0f).yaw == right.yaw && door::aimFromAxes(3.0f, -3.0f).pitch == door::aimFromAxes(0.0f, -1.0f).pitch, "mount: beyond the axes' range it stays at the limits", 0.0f);
    float at = 0.0f;
    for (int k = 0; k < 60; ++k) at += (1.0f - at) * door::follow(1.0f / 60.0f);
    check(at > 0.99f && door::follow(1.0f) <= 1.0f, "mount: the view is right behind the gun within a second, stable at any frame rate", at);

    const std::string pattern = "GSG Door Gun";
    check(door::isMount("Hind GSG Door Gun", pattern) && door::isMount("GSG Door Gun", pattern), "mount: found with the maker's name before the product's", 0.0f);
    check(!door::isMount("Thrustmaster T.16000M", pattern) && !door::isMount("Anything", ""), "mount: not another stick, nor anything when not asked for", 0.0f);
    const std::vector<std::string> gunFirst = {"Hind GSG Door Gun", "T.16000M", "Pedals"};
    check(door::findMount(gunFirst, pattern) == 0, "mount: the mount is found", 0.0f);
    check(door::pickMain(gunFirst, "", 0, pattern) == 1, "mount: plugged in first it does not take the cyclic", 0.0f);
    check(door::pickMain(gunFirst, "", 1, pattern) == 2, "mount: [joystick] device counts the other devices", 0.0f);
    check(door::pickMain({"Hind GSG Door Gun"}, "", 0, pattern) == -1, "mount: alone, there is no main device", 0.0f);
    check(door::pickMain({"T.16000M", "Pedals"}, "Pedals", 0, pattern) == 1, "mount: device_name still chooses", 0.0f);
    check(door::pickMain({"T.16000M", "Pedals"}, "", 5, pattern) == 1, "mount: device index stays in range without a mount", 0.0f);
    check(door::pickMain({}, "", 0, pattern) == -1, "mount: no devices, no main device", 0.0f);
    check(door::pickMain(gunFirst, "Hind GSG Door Gun", 0, pattern) == 0, "mount: named as the main device, it is", 0.0f);
  }
  {
    // A drone shot down falls on along its way, the nose along its path, spinning once a second
    // about its length, and lies still on the ground.
    sim::VehicleType dt;
    dt.name = dt.original = "TEST DRONE";
    dt.cls = sim::UnitClass::Drone;
    dt.health = 2.5f;
    dt.lo = {-1.25f, -0.3f, -1.75f}, dt.hi = {1.25f, 0.3f, 1.75f};
    sim::Targets t(terrain);
    const int i = t.add(sim::registerVehicleType(dt), 0.0f, 0.0f, glm::radians(90.0f));
    sim::Vehicle& v = t.list[size_t(i)];
    const float ground = v.pos.y;
    v.pos.y += 150.0f;
    v.vel = {50.0f, 0.0f, 0.0f};  // east, as it heads
    t.destroy(i, sim::Killer::Player);
    const glm::vec3 from = v.pos;
    for (int k = 0; k < 120; ++k) t.step(1.0f / 120.0f);
    check(v.pos.x - from.x > 40.0f && from.y - v.pos.y > 3.0f, "wreck: a drone shot down flies on as it falls (m east)", v.pos.x - from.x);
    const glm::mat4 m = v.toWorld();
    const glm::vec3 nose = glm::normalize(glm::vec3(m * glm::vec4(0, 0, -1, 0))), path = glm::normalize(v.vel);
    check(glm::dot(nose, path) > 0.999f && nose.y < -0.1f, "wreck: its nose along its path, down", nose.y);
    // A quarter of a turn on (a quarter of a second), its wing tip where its belly was.
    const glm::vec3 wing0 = glm::vec3(m * glm::vec4(1, 0, 0, 0));
    sim::Vehicle later = v;
    later.sinceDestroyed += 0.25f;
    const glm::vec3 wing1 = glm::vec3(later.toWorld() * glm::vec4(1, 0, 0, 0));
    check(std::fabs(glm::dot(wing0, wing1)) < 0.05f, "wreck: it spins 1 turn a second about its length", glm::dot(wing0, wing1));
    for (int k = 0; k < 120 * 20; ++k) t.step(1.0f / 120.0f);
    check(v.pos.y <= ground + 0.5f && glm::length(v.vel) == 0.0f && !v.fallingDrone(), "wreck: on the ground it lies still", v.pos.y - ground);
  }
  {
    // The rows painted over the original's cockpit pictures (seen looking up): as wide as those,
    // put on top of them.
    for (const char* name : {"pilot-top.pam", "gunner-top.pam"}) {
      gamedata::Picture top, pic;
      std::string error;
      const bool ok = gamedata::loadPam(std::string(HIND_DATA_DIR) + "/cockpit/" + name, top, error);
      check(ok && top.width == 1920 && top.height > 50 && top.height < 120, "cockpit: rows over the picture load (rows)", top.height);
      pic.width = 1920, pic.height = 480;
      pic.rgba.assign(size_t(1920) * 480 * 4, 7);
      check(gamedata::stackOver(top, pic) && pic.height == 480 + top.height && pic.rgba.back() == 7 && pic.rgba.size() == size_t(1920) * size_t(pic.height) * 4,
            "cockpit: they go on top of the original's picture", pic.height);
    }
    gamedata::Picture bad;
    std::string error;
    check(!gamedata::loadPam(std::string(HIND_DATA_DIR) + "/missions", bad, error) && !error.empty(), "cockpit: no picture, no load", 0.0f);
  }

  std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
