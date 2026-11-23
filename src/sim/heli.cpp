#include "sim/heli.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sim {

namespace {

constexpr float kAirDensity = 1.225f;
constexpr float kRefWeight = Helicopter::kRefMass * Helicopter::kGravity;

// Inertia (kg m^2) about body x (pitch), y (yaw), z (roll).
const glm::vec3 kInertia{60000.0f, 70000.0f, 15000.0f};

const glm::vec3 kHub{0.0f, 2.22f, 0.0f};
constexpr float kRotorRadius = 8.65f;
constexpr float kRotorRevPerSec = 3.2f;  // ~192 rpm

// Landing gear contact points (body frame).
struct Gear {
  glm::vec3 p;
  float sideFriction;  // nose wheels castor, mains do not
};
const std::array<Gear, 3> kGear{{
    {{0.0f, -1.85f, -3.09f}, 0.05f},  // nose wheels sit lower: parked nose-up, as on the real one
    {{-1.55f, -1.6f, 1.22f}, 0.8f},
    {{1.55f, -1.6f, 1.22f}, 0.8f},
}};
constexpr float kGearSpring = 240000.0f;
constexpr float kGearDamper = 36000.0f;

// Parts that must never touch the ground.
struct Probe {
  glm::vec3 p;
  const char* what;
};
const std::array<Probe, 5> kProbes{{
    {{0.0f, -1.0f, -5.75f}, "NOSE STRIKE"},
    {{0.0f, -0.44f, 9.92f}, "TAIL STRIKE"},
    {{-0.68f, 0.21f, 10.41f}, "TAIL ROTOR STRIKE"},
    {{0.0f, -1.22f, -2.0f}, "GEAR UP LANDING"},
    {{0.0f, -1.22f, 2.0f}, "GEAR UP LANDING"},
}};

float sq(float x) { return x * x; }

float smoothstep(float a, float b, float x) {
  float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float densityRatio(float altitude) { return std::exp(-std::max(altitude, 0.0f) / 8500.0f); }

// Angular velocity commanding a change of heading (about world up), expressed in body axes.
glm::vec3 headingRateBody(const glm::quat& q, float headingRate) {
  return glm::inverse(q) * glm::vec3(0.0f, -headingRate, 0.0f);
}

}  // namespace

const char* flightModelName(FlightModel m) {
  switch (m) {
    case FlightModel::Novice: return "NOVICE";
    case FlightModel::Stable: return "STABLE";
    case FlightModel::Realistic: return "REALISTIC";
  }
  return "?";
}

bool parseFlightModel(const std::string& s, FlightModel& out) {
  if (s == "novice") out = FlightModel::Novice;
  else if (s == "stable") out = FlightModel::Stable;
  else if (s == "realistic") out = FlightModel::Realistic;
  else return false;
  return true;
}

Helicopter::Helicopter(const world::Terrain& terrain) : terrain_(terrain) {}

// Rotor thrust per unit collective relative to the hover, as a function of speed through the disc
// plane. Rises with translational lift up to ~135 km/h, then falls as profile and parasite power
// grow (expressed here as reduced efficiency so that torque tracks the collective lever).
float Helicopter::rotorEfficiency(float v) {
  static const std::array<std::pair<float, float>, 11> table{{
      {0.0f, 1.00f}, {10.0f, 1.05f}, {20.0f, 1.18f}, {30.0f, 1.30f}, {37.5f, 1.37f}, {50.0f, 1.25f},
      {65.0f, 1.02f}, {83.0f, 0.79f}, {93.0f, 0.695f}, {105.0f, 0.50f}, {120.0f, 0.32f},
  }};
  if (v <= table.front().first) return table.front().second;
  for (size_t i = 1; i < table.size(); ++i) {
    if (v <= table[i].first) {
      float t = (v - table[i - 1].first) / (table[i].first - table[i - 1].first);
      return table[i - 1].second + (table[i].second - table[i - 1].second) * t;
    }
  }
  return table.back().second;
}

float Helicopter::thrustFactor(float c, float inplane, float axial, float hubAgl, float sigma) {
  float groundEffect = 1.0f + 0.2f * sq(std::max(0.0f, 1.0f - std::max(hubAgl, 0.0f) / 17.0f));
  // Climbing reduces thrust (more inflow), descending increases it. The effect fades in forward
  // flight where the mass flow through the disc is dominated by forward speed.
  float hoverness = 1.0f / (1.0f + sq(inplane / 15.0f));
  float inflow = axial > 0.0f ? axial / 9.0f : axial / 15.0f;
  float t = (c / kHoverCollective) * rotorEfficiency(inplane) * groundEffect * sigma - inflow * hoverness;
  return std::max(t, 0.0f);
}

glm::vec3 Helicopter::aeroForceBody(const glm::vec3& vb, float sigma) {
  float rho = kAirDensity * sigma;
  // Equivalent flat-plate areas: side, top, front.
  const glm::vec3 area{40.0f, 40.0f, 3.5f};
  glm::vec3 f = -0.5f * rho * area * vb * glm::abs(vb);
  // Stub wings unload the rotor at speed (~20 % of weight at Vmax).
  float vf = -vb.z;
  if (vf > 1.0f) {
    float aoa = std::atan2(-vb.y, vf);
    float cl = std::clamp(1.0f + 2.5f * aoa, 0.0f, 1.6f);
    f.y += 3.99f * sigma * vf * vf * cl;
  }
  return f;
}

float Helicopter::levelFlightCollective(float speed, float mass, float* pitchOut) {
  float w = mass * kGravity;
  float t = w, vy = 0.0f, inplane = speed;
  float lo = 0.0f, hi = 0.6f, theta = 0.0f;
  // Bisection on nose-down pitch so that thrust tilt balances horizontal aero force.
  for (int i = 0; i < 60; ++i) {
    theta = 0.5f * (lo + hi);
    float s = std::sin(theta), c = std::cos(theta);
    glm::vec3 vb{0.0f, speed * s, -speed * c};  // flying along world -z, nose down by theta
    glm::vec3 fb = aeroForceBody(vb, 1.0f);
    // body -> world for a pitch of -theta about x
    float fwy = fb.y * c + fb.z * s;
    float fwz = -fb.y * s + fb.z * c;
    t = (w - fwy) / c;
    float horizontal = fwz - t * s;  // thrust tilts forward (-z)
    if (horizontal > 0.0f) lo = theta;
    else hi = theta;
    vy = vb.y;
    inplane = std::fabs(vb.z);
  }
  if (pitchOut) *pitchOut = theta;
  // Solve thrustFactor(c) = t / refWeight for c (hub far above ground, sea level).
  float hoverness = 1.0f / (1.0f + sq(inplane / 15.0f));
  float inflow = vy > 0.0f ? vy / 9.0f : vy / 15.0f;
  return (t / kRefWeight + inflow * hoverness) * kHoverCollective / rotorEfficiency(inplane);
}

void Helicopter::resetOnGround(glm::vec2 xz, float headingRad) {
  float g = terrain_.groundHeight(xz.x, xz.y);
  pos = {xz.x, g + kRestHeight, xz.y};
  vel = glm::vec3(0.0f);
  omega = glm::vec3(0.0f);
  orient = glm::angleAxis(-headingRad, glm::vec3(0, 1, 0));
  gearDown = true;
  gearPos = 1.0f;
  brakes = true;
  crashed = false;
  crashCause.clear();
  collective = torque = 0.0f;
  fuel = kFuelFull;
  engineRpm = 78.0f;
  rotorRpm = 100.0f;
  rotorSpeed_ = 1.0f, enginesIdle = false, rotorFree = false, spool_ = 1.0f;
  weightOnWheels = 1.0f;
  wasOnGround_ = true;
  novSpeedTarget_ = 0.0f;
  novHoldAgl_ = 0.0f;
  damage = Damage();
  message.clear();
}

void Helicopter::resetAirborne(glm::vec3 position, float headingRad, float speed) {
  pos = position;
  orient = glm::angleAxis(-headingRad, glm::vec3(0, 1, 0));
  vel = forward() * speed;
  omega = glm::vec3(0.0f);
  crashed = false;
  crashCause.clear();
  brakes = false;
  fuel = kFuelFull;
  engineRpm = 92.0f;
  rotorRpm = 100.0f;
  rotorSpeed_ = 1.0f, enginesIdle = false, rotorFree = false, spool_ = 1.0f;
  weightOnWheels = 0.0f;
  wasOnGround_ = false;
  novSpeedTarget_ = speed;
  novHoldAgl_ = radarAltitude();
  damage = Damage();
  message.clear();
}

const char* systemFailure(System s) {
  static const char* names[kSystems] = {"NAV FAILURE", "RADIO FAILURE", "JAMMER FAILURE", "THREAT WARNING FAILURE",
                                        "HUD FAILURE", "LLTV FAILURE", "FLIR FAILURE", "GUN FAILURE",
                                        "HMTD FAILURE", "LASER FAILURE", "GEAR DAMAGED"};
  return names[int(s)];
}

void Helicopter::fail(System s) {
  if (crashed || noCrash || damage.failed[int(s)]) return;
  damage.failed[int(s)] = true;
  setMessage(systemFailure(s), 3.0f);
}

void Helicopter::hit(HitPart part, float points) {
  if (crashed || noCrash || damage.shotDown || points <= 0.0f) return;
  Damage& d = damage;
  const char* what = nullptr;
  switch (part) {
    case HitPart::Engine1:
    case HitPart::Engine2: {
      const int i = part == HitPart::Engine1 ? 0 : 1;
      const bool wasOut = engineOut(i);
      d.engine[i] = std::max(0.0f, d.engine[i] - points);
      if (!wasOut) what = engineOut(i) ? (i ? "ENGINE 2 FAILED" : "ENGINE 1 FAILED") : engineFire(i) ? (i ? "ENGINE 2 FIRE" : "ENGINE 1 FIRE")
                                                                                     : (i ? "ENGINE 2 HIT" : "ENGINE 1 HIT");
      d.airframe -= points * 0.2f;
      break;
    }
    case HitPart::TailRotor: {
      const bool was = d.tailRotor > 0.0f;
      d.tailRotor = std::max(0.0f, d.tailRotor - points);
      what = d.tailRotor <= 0.0f ? (was ? "TAIL ROTOR LOST" : nullptr) : "TAIL ROTOR HIT";
      d.airframe -= points * 0.2f;
      break;
    }
    case HitPart::FuelTank:
      d.fuelLeak = std::min(d.fuelLeak + points * 0.05f, 3.0f);
      what = "FUEL LEAK";
      d.airframe -= points * 0.3f;
      break;
    case HitPart::Airframe:
      d.airframe -= points;
      if (points >= 2.0f) what = "HIT";
      break;
  }
  d.airframe = std::max(d.airframe, 0.0f);
  if (d.airframe <= 0.0f && !d.shotDown) {
    // Out of control: the engines and the tail rotor go, it falls.
    d.airframe = 0.0f;
    d.shotDown = true;
    d.engine[0] = d.engine[1] = d.tailRotor = 0.0f;
    what = "SHOT DOWN";
  }
  if (what) setMessage(what, d.shotDown ? 1e9f : 2.5f);
}

void Helicopter::toggleEngines() {
  if (model == FlightModel::Novice) {
    setMessage("NO AUTOROTATION IN NOVICE", 2.0f);
    return;
  }
  enginesIdle = !enginesIdle;
  setMessage(enginesIdle ? "ENGINES IDLE - ROTOR FREE" : "ENGINES ENGAGED", 2.5f);
}

void Helicopter::toggleGear() {
  if (!works(System::Gear)) {
    setMessage(systemFailure(System::Gear), 2.0f);
    return;
  }
  if (radarAltitude() < 3.0f && gearDown) {
    setMessage("GEAR LOCKED ON GROUND");
    return;
  }
  gearDown = !gearDown;
  setMessage(gearDown ? "GEAR DOWN" : "GEAR UP", 2.0f);  // (as the original's "Gear Down", "Gear Up")
}

void Helicopter::setMessage(const std::string& text, float seconds) {
  message = text;
  messageTime = seconds;
}

float Helicopter::headingDeg() const {
  glm::vec3 f = forward();
  float h = glm::degrees(std::atan2(f.x, -f.z));
  return h < 0.0f ? h + 360.0f : h;
}

float Helicopter::pitchDeg() const { return glm::degrees(std::asin(std::clamp(forward().y, -1.0f, 1.0f))); }

float Helicopter::rollDeg() const {
  glm::vec3 r = right(), u = up();
  return glm::degrees(std::atan2(-r.y, u.y));
}

float Helicopter::radarAltitude() const {
  return std::max(0.0f, pos.y - kRestHeight - terrain_.groundHeight(pos.x, pos.z));
}

const std::vector<glm::vec3>& Helicopter::outline() {
  // The middle first; the nose, the tail and its rotor, the stub wings' tips, and the main rotor
  // tips all round (8.65 m from the hub, 2.3 m above the middle).
  static const std::vector<glm::vec3> pts = [] {
    std::vector<glm::vec3> p{{0.0f, 0.0f, 0.0f}, {0.0f, -0.6f, -8.0f}, {0.0f, 1.0f, 9.5f}, {-3.2f, -0.3f, -0.5f}, {3.2f, -0.3f, -0.5f}};
    for (int k = 0; k < 12; ++k) {
      const float a = float(k) * 0.5236f;
      p.push_back({8.6f * std::sin(a), 2.3f, 8.6f * std::cos(a)});
    }
    return p;
  }();
  return pts;
}

void Helicopter::crash(const char* why) {
  if (noCrash) return;
  crashed = true;
  crashCause = why;
  crashVel = vel;
  vel = glm::vec3(0.0f);
  omega = glm::vec3(0.0f);
  setMessage(std::string(why) + " - PRESS R", 1e9f);
}

void Helicopter::step(const Controls& in, float dt) {
  time_ += dt;
  if (messageTime > 0.0f) {
    messageTime -= dt;
    if (messageTime <= 0.0f) message.clear();
  }
  float rotorSpeed = crashed ? 0.0f : kRotorRevPerSec;
  rotorAngle = std::fmod(rotorAngle + rotorSpeed * 2.0f * glm::pi<float>() * dt, 2.0f * glm::pi<float>());
  tailRotorAngle = std::fmod(tailRotorAngle + rotorSpeed * 5.6f * 2.0f * glm::pi<float>() * dt,
                             2.0f * glm::pi<float>());
  if (crashed) {
    engineRpm = std::max(0.0f, engineRpm - 20.0f * dt);
    rotorRpm = std::max(0.0f, rotorRpm - 20.0f * dt);
    return;
  }

  // A failed undercarriage stays where it was (half way, if it was moving).
  if (works(System::Gear)) gearPos = std::clamp(gearPos + (gearDown ? dt : -dt) * 0.2f, 0.0f, 1.0f);
  // One engine gives some 60 % of the torque of two, none (or no fuel) nothing.
  const int engines = int(!engineOut(0)) + int(!engineOut(1));
  const float power = fuel <= 0.0f ? 0.0f : engines == 2 ? 1.0f : engines == 1 ? 0.6f : 0.0f;
  if (model == FlightModel::Novice) enginesIdle = false;
  // Nothing drives the rotor (disengaged, failed, out of fuel): it turns on what it has, and the
  // lever still pitches the blades. (A shot down helicopter has lost it; Novice has no such lever.)
  const bool driven = power > 0.0f && !enginesIdle;
  const bool freeRotor = !driven && model != FlightModel::Novice && !damage.shotDown;
  rotorFree = freeRotor;
  // Engaged again the engines need some seconds to give all their power.
  spool_ = driven ? std::min(spool_ + dt / kEngineSpool, 1.0f) : 0.0f;
  collective = std::clamp(in.collective, 0.0f, freeRotor ? kMaxCollective : kMaxCollective * power * (0.3f + 0.7f * spool_));
  if (damage.fuelLeak > 0.0f && !infiniteFuel) fuel = std::max(0.0f, fuel - damage.fuelLeak * dt);
  const float tailRotor = damage.tailRotor / 100.0f;

  const glm::mat3 R = glm::mat3_cast(orient);
  const glm::mat3 Rt = glm::transpose(R);
  const glm::vec3 vb = Rt * vel;
  const float sigma = densityRatio(pos.y);
  const float weight = mass * kGravity;
  const glm::vec3 hubW = pos + R * kHub;
  const float hubAgl = hubW.y - groundAt(hubW);
  const float inplane = std::sqrt(vb.x * vb.x + vb.z * vb.z);
  const float tailEff = 1.0f / (1.0f + sq(inplane / 30.0f));
  const float pitch = glm::radians(pitchDeg());
  const float roll = glm::radians(rollDeg());
  const float wow = weightOnWheels;

  glm::vec3 force{0.0f, -weight, 0.0f};
  glm::vec3 aeroW = R * aeroForceBody(vb, sigma);
  force += aeroW;

  // Horizontal frame aligned with the nose.
  glm::vec3 fwdH = forward();
  fwdH.y = 0.0f;
  fwdH = glm::length(fwdH) > 1e-3f ? glm::normalize(fwdH) : glm::vec3(0, 0, -1);
  glm::vec3 rightH{-fwdH.z, 0.0f, fwdH.x};
  glm::vec3 velH{vel.x, 0.0f, vel.z};
  float speedH = glm::length(velH);
  float vFwd = glm::dot(velH, fwdH), vSide = glm::dot(velH, rightH);

  // ---------------- rotor thrust
  glm::vec3 thrustDirB{0.0f, 1.0f, 0.0f};
  float thrust;
  float radarAlt = radarAltitude();
  if (model == FlightModel::Novice) {
    // The collective lever commands climb rate; centred (50 %) holds height above ground.
    float vsCmd = std::clamp((collective - 0.5f) / 0.5f * 8.0f, -8.0f, 12.0f);
    float aglAhead = radarAlt;
    for (float t : {1.0f, 2.0f, 3.0f}) {
      glm::vec3 p = pos + velH * t;
      aglAhead = std::min(aglAhead, pos.y - kRestHeight - groundAt(p));
    }
    if (std::fabs(vsCmd) > 0.4f || wow > 0.5f) novHoldAgl_ = radarAlt;
    else vsCmd = std::clamp((novHoldAgl_ - aglAhead) * 0.8f, -6.0f, 8.0f);
    if (radarAlt < 12.0f) vsCmd = std::max(vsCmd, -(1.0f + radarAlt * 0.4f));  // cushion the touchdown
    float aVert = std::clamp((vsCmd - vel.y) * 1.5f, -6.0f, 6.0f);
    float upY = std::max((R * thrustDirB).y, 0.3f);
    float maxThrust = kRefWeight * thrustFactor(kMaxCollective * power, inplane, 0.0f, hubAgl, sigma);
    thrust = std::clamp((mass * (kGravity + aVert) - aeroW.y) / upY, 0.0f, maxThrust);
    // Once the wheels touch while descending, unload the rotor so the helicopter settles.
    novSettle_ = (wow > 0.02f && vsCmd < 0.0f) ? novSettle_ + dt : 0.0f;
    thrust *= std::max(0.0f, 1.0f - novSettle_ / 2.0f);
    // Standing on the wheels the lever must be raised above the middle to lift off; around 50 %
    // the rotor only makes the helicopter light enough to taxi.
    if (wasOnGround_ && vsCmd < 0.4f) thrust = std::min(thrust, 0.7f * weight);
  } else {
    // Cyclic also tilts the disc a little, giving direct translational control.
    thrustDirB = glm::normalize(glm::vec3(in.cyclicX * 0.06f, 1.0f, -in.cyclicY * 0.06f));
    // Stable: the disc flaps back from the air going past it (as a real rotor's), tilting the
    // thrust against the drift without pitching the airframe: hands off it settles in the hover
    // instead of sliding off backwards or sideways. Strongest at walking pace, gone by 100 km/h
    // (the top speed stays the original's).
    if (model == FlightModel::Stable && wow <= 0.0f) {
      const glm::vec2 drift{vb.x, vb.z};
      const float slow = glm::dot(drift, drift) / 36.0f;
      const glm::vec2 flap = -0.012f * drift / (1.0f + slow * slow);
      thrustDirB = glm::normalize(thrustDirB + glm::vec3(flap.x, 0.0f, flap.y));
    }
    // Thrust goes with the square of the rotor's speed, and a rotor too slow stalls.
    const float rotorLift = rotorSpeed_ * rotorSpeed_ * smoothstep(kRotorStall, kRotorStall + 0.25f, rotorSpeed_);
    thrust = kRefWeight * thrustFactor(collective, inplane, vb.y, hubAgl, sigma) * rotorLift;
  }
  if (freeRotor) {
    // The rotor's energy: air coming up through the disc drives it, the blades' pitch (the lever)
    // and their drag take it. So descending or flaring raises the revolutions, the lever spends them.
    const float w = rotorSpeed_;
    const float drive = kRotorDrive * w * w * std::max(-vb.y, 0.0f);
    const float spend = kRotorPower * (0.12f * w * w * w + collective * w);
    rotorSpeed_ = std::clamp(w + (drive - spend) / (kRotorEnergy * std::max(w, 0.05f)) * dt, 0.0f, kRotorMax);
    if (rotorSpeed_ > 1.12f && message.empty()) setMessage("ROTOR OVERSPEED", 0.5f);
  } else if (driven) {
    rotorSpeed_ = std::min(rotorSpeed_ + dt * 0.25f, 1.0f);
  }
  // On the wheels the airframe cannot pitch, so the cyclic tilts the disc further and pulls the
  // helicopter along the ground: taxiing with the brakes off, as in the original.
  if (wasOnGround_) {
    glm::vec3 tilt{in.cyclicX * 0.12f, 0.0f, -in.cyclicY * 0.12f};
    thrustDirB = glm::normalize(thrustDirB + tilt);
  }
  force += R * thrustDirB * thrust;
  torque = freeRotor ? 0.0f : collective * 100.0f;
  // Past the limit the text display says so (as the original, with its TORQ lamp lit), unless
  // it has something else to say.
  if (torque > 100.5f && (message.empty() || message == "ENGINE OVRTORQ")) setMessage("ENGINE OVRTORQ", 0.5f);
  // Two TV3-117s: about 200 kg/h at flight idle, 900 kg/h at full power. The governors hold the
  // rotor at 100 % until the torque is pulled past the limit.
  if (!infiniteFuel) fuel = std::max(0.0f, fuel - (200.0f + 5.5f * torque) / 3600.0f * dt);
  const bool running = power > 0.0f;
  const float lag = 1.0f - std::exp(-dt / 1.5f);
  engineRpm += ((running ? (driven ? 78.0f + 0.2f * torque : 65.0f) : 0.0f) - engineRpm) * lag;
  if (freeRotor) rotorRpm = rotorSpeed_ * 100.0f;
  else rotorRpm += ((running ? 100.0f * rotorSpeed_ - std::max(0.0f, torque - 105.0f) * 0.5f : 0.0f) - rotorRpm) * lag;

  // ---------------- attitude: desired body rates
  glm::vec3 rateCmd{0.0f};
  glm::vec3 accelExtra{0.0f};
  // How fast the body rates follow the controls (1/s: pitch, yaw, roll). A heavy helicopter with
  // an articulated rotor (small hinge offset, little damping from the disc) and a long fuselage
  // answers slowly, and slower in pitch than in roll: pitch settles in about a second, roll in
  // half of one. The novice model stays brisk.
  const glm::vec3 kResponse = model == FlightModel::Novice     ? glm::vec3{4.0f, 3.0f, 6.0f}
                              : model == FlightModel::Stable ? glm::vec3{1.4f, 3.0f, 2.5f}
                                                             : glm::vec3{1.0f, 3.0f, 2.0f};
  // Full cyclic's body rates (rad/s) in the stable and realistic models.
  const bool stableModel = model == FlightModel::Stable;
  const float maxPitchRate = stableModel ? 0.38f : 0.32f, maxRollRate = stableModel ? 0.7f : 0.6f;
  float coord = smoothstep(15.0f, 30.0f, speedH);

  if (model == FlightModel::Novice) {
    if (wow > 0.5f) novSpeedTarget_ = vFwd;
    novSpeedTarget_ = std::clamp(novSpeedTarget_ + in.cyclicY * 12.0f * dt, -12.0f, 95.0f);
    float aFwd = std::clamp((novSpeedTarget_ - vFwd) * 0.5f, -3.5f, 3.5f);
    float dragA = glm::dot(aeroW, fwdH) / mass;
    float pitchTarget = std::clamp(-std::atan((aFwd - dragA) / kGravity), -0.35f, 0.35f);
    float lowSpeedRoll = std::abs(in.cyclicX) > 0.05f ? in.cyclicX * 0.35f : std::clamp(-vSide * 0.06f, -0.2f, 0.2f);
    float rollTarget = lowSpeedRoll + (in.cyclicX * 0.6f - lowSpeedRoll) * coord;
    float pitchRate = std::clamp(2.5f * (pitchTarget - pitch), -0.6f, 0.6f);
    float rollRate = std::clamp(3.0f * (rollTarget - roll), -1.0f, 1.0f);
    // The pedals yaw about the rotor mast (body y), the turn the bank makes about the vertical.
    float yawRate = in.pedal * 0.8f * tailEff * tailRotor;
    float turnRate = coord * kGravity * std::tan(roll) / std::max(speedH, 10.0f);
    rateCmd = headingRateBody(orient, turnRate) + glm::vec3(pitchRate, -yawRate, -rollRate);
    // Keep the flight path aligned with the nose at speed.
    force += -rightH * vSide * mass * 0.8f * coord;
  } else {
    bool stable = model == FlightModel::Stable;
    float level = stable ? 1.0f : 0.0f;
    // Stable: centred cyclic holds the pitch attitude (so the keyboard can fly), wings self-level.
    // Full cyclic: about 18 deg/s of pitch (stable 22), 35 of roll (stable 40).
    float pitchRate = -in.cyclicY * maxPitchRate;
    // Lifting off hands off, it does not keep the nose-up the gear and the ground left it at
    // (a few degrees back the thrust drives it off backwards): it levels, until the cyclic moves.
    if (stable) {
      if (wow > 0.0f) levelling_ = true;
      if (std::fabs(in.cyclicY) > 0.05f || (wow <= 0.0f && std::fabs(pitch) < glm::radians(0.1f))) levelling_ = false;
      if (levelling_ && wow <= 0.0f) pitchRate = std::clamp(-0.8f * pitch, -0.1f, 0.1f);
    }
    // (Not while the pedals turn it slowly: a stall turn goes through a steep bank on the way,
    // which levelled would stop the nose short of coming down.)
    const float pedalTurn = std::min(1.0f, std::fabs(in.pedal) * 3.0f) * (1.0f - smoothstep(15.0f, 30.0f, speedH));
    float rollRate = in.cyclicX * maxRollRate - level * (1.0f - pedalTurn) * 0.3f * roll;
    // In the realistic model the tail rotor keeps most of its authority with speed; the fin
    // (below) limits the sideslip a full pedal holds: ~50 deg at 100 km/h, ~10 at 200.
    float pedalEff = stable ? tailEff : 0.6f + 0.4f / (1.0f + sq(inplane / 12.0f));
    // The pedals yaw about the rotor mast (body y), so nose up they swing it over (a stall turn);
    // the turn the bank makes in the stable model is about the vertical.
    float yawRate = in.pedal * 0.9f * pedalEff * tailRotor;
    float turnRate = stable ? 0.5f * coord * kGravity * std::tan(roll) / std::max(speedH, 10.0f) : 0.0f;
    rateCmd = headingRateBody(orient, turnRate) + glm::vec3(pitchRate, -yawRate, -rollRate);
    if (!stable) {
      // Main rotor torque reaction (nose left with collective), rigged neutral at ~55 %.
      if (!freeRotor) accelExtra.y += 0.5f * (collective - 0.55f) * (1.0f - wow);
      // Blowback: the neutral cyclic position moves forward with speed.
      accelExtra.x += 0.004f * std::max(-vb.z, 0.0f);
      // Hover turbulence.
      float gust = (1.0f - smoothstep(10.0f, 30.0f, inplane)) * (1.0f - wow);
      accelExtra.x += gust * 0.06f * std::sin(time_ * 1.3f) * std::sin(time_ * 0.37f + 1.0f);
      accelExtra.z += gust * 0.08f * std::sin(time_ * 1.7f + 2.0f) * std::sin(time_ * 0.29f);
      accelExtra.y += gust * 0.04f * std::sin(time_ * 0.9f + 4.0f);
    }
  }
  // Fin weathercock stability; tailplane pitch stability only in the realistic model.
  float vfwd = std::max(-vb.z, 0.0f);
  if (model == FlightModel::Realistic) {
    // The fin's moment grows with the dynamic pressure and the sine of the sideslip, so it also
    // swings the tail back round from large sideslip angles.
    accelExtra.y += -0.0028f * vb.x * inplane;
    accelExtra.x += 0.0005f * vb.y * vfwd;
    // Sideslip rolls the helicopter away from the relative wind (towards the pedal pushed), so
    // the pedal at speed starts a turn as in the real one.
    accelExtra.z += 0.0008f * vb.x * vfwd;
  } else {
    accelExtra.y += -0.0025f * vb.x * vfwd;
  }

  // Without the tail rotor nothing holds the heading but the fin: the torque turns the nose left.
  glm::vec3 response = kResponse;
  // (The disturbances above were sized against the brisk response and the faster rates of full
  // cyclic (0.55, 0.9 rad/s): scaled so they still take the same stick to hold.)
  if (model != FlightModel::Novice)
    accelExtra.x *= response.x / 4.0f * maxPitchRate / 0.55f, accelExtra.z *= response.z / 6.0f * maxRollRate / 0.9f;
  if (tailRotor < 1.0f) {
    response.y *= 0.1f + 0.9f * tailRotor;
    accelExtra.y += (1.0f - tailRotor) * 1.6f * collective * (1.0f - wow);
  }

  // Braked wheels hold the heading; unbraked, the tail rotor can pivot the helicopter slowly when
  // standing and steers it normally while taxiing.
  float yawAuthority = brakes ? 0.0f : 0.3f + 0.7f * smoothstep(0.5f, 4.0f, std::fabs(vFwd));
  // Even light on the wheels (taxiing with the lever half up) the gear holds the attitude.
  const float onWheels = std::min(wow * 4.0f, 1.0f);
  rateCmd.y *= 1.0f - std::min(wow * 8.0f, 1.0f) * (1.0f - yawAuthority);
  glm::vec3 ctrlAccel = response * (rateCmd - omega);
  // On the ground the wheels own pitch and roll.
  ctrlAccel.x *= 1.0f - 0.9f * onWheels;
  ctrlAccel.z *= 1.0f - 0.9f * onWheels;
  accelExtra.x *= 1.0f - onWheels;
  accelExtra.z *= 1.0f - onWheels;

  // ---------------- landing gear and ground
  glm::vec3 gearTorque{0.0f};
  float normalSum = 0.0f;
  float maxImpact = 0.0f, maxSlide = 0.0f;
  bool inWater = false;
  bool gearActive = gearPos > 0.95f || noCrash;
  if (gearActive) {
    for (const Gear& g : kGear) {
      glm::vec3 r = R * g.p;
      glm::vec3 p = pos + r;
      float gh = groundAt(p);
      if (p.y >= gh) continue;
      glm::vec3 n = terrain_.groundNormal(p.x, p.z);
      float pen = (gh - p.y) * n.y;
      glm::vec3 vp = vel + glm::cross(R * omega, r);
      float vn = glm::dot(vp, n);
      maxImpact = std::max(maxImpact, -vn);
      float fn = std::max(0.0f, kGearSpring * pen - kGearDamper * vn);
      glm::vec3 vt = vp - n * vn;
      glm::vec3 rollDir = forward() - n * glm::dot(forward(), n);
      rollDir = glm::normalize(rollDir);
      glm::vec3 sideDir = glm::cross(n, rollDir);
      float vRoll = glm::dot(vt, rollDir), vS = glm::dot(vt, sideDir);
      float muRoll = brakes ? 0.5f : 0.03f;
      float fRoll = -vRoll / (std::fabs(vRoll) + 0.05f) * muRoll * fn;
      float fSide = -vS / (std::fabs(vS) + 0.05f) * g.sideFriction * fn;
      glm::vec3 f = n * fn + rollDir * fRoll + sideDir * fSide;
      force += f;
      gearTorque += glm::cross(g.p, Rt * f);
      normalSum += fn;
      if (g.sideFriction > 0.5f) maxSlide = std::max(maxSlide, std::max(std::fabs(vS), -vRoll));
      if (gh < 0.05f) inWater = true;
    }
  }
  weightOnWheels = std::clamp(normalSum / weight, 0.0f, 1.0f);
  bool onGround = normalSum > 0.0f;

  if (onGround && !wasOnGround_) {
    if (maxImpact > 4.5f) crash("CRASHED");
    else if (maxSlide > 5.0f) crash("CRASHED - SIDEWAYS LANDING");
    else if (maxImpact > 2.0f) setMessage("HARD LANDING");
  }
  if (onGround && inWater) crash("DITCHED");
  // Shot down it does not land: it hits the ground and goes up.
  if (onGround && damage.shotDown) crash("CRASHED");
  if (onGround && (std::fabs(roll) > glm::radians(30.0f) || std::fabs(pitch) > glm::radians(25.0f)))
    crash("ROLLED OVER");
  if (crashed) return;
  wasOnGround_ = onGround;

  for (const Probe& pr : kProbes) {
    bool belly = pr.p.y < -1.0f;
    if (belly && gearActive) continue;
    glm::vec3 p = pos + R * pr.p;
    if (p.y < groundAt(p)) {
      crash(pr.what);
      if (crashed) return;
    }
  }
  for (int i = 0; i < 8; ++i) {
    float a = rotorAngle + i * glm::pi<float>() / 4.0f;
    glm::vec3 tip = pos + R * (kHub + kRotorRadius * glm::vec3(std::cos(a), 0.0f, std::sin(a)));
    if (tip.y < groundAt(tip)) {
      crash("ROTOR STRIKE");
      if (crashed) return;
    }
  }

  // ---------------- integrate
  glm::vec3 gyro = glm::cross(omega, kInertia * omega);
  glm::vec3 angAccel = ctrlAccel + accelExtra + (gearTorque - gyro) / kInertia;
  omega += angAccel * dt;
  vel += force / mass * dt;
  pos += vel * dt;
  glm::quat w{0.0f, omega.x, omega.y, omega.z};
  orient = glm::normalize(orient + (orient * w) * (0.5f * dt));
}

}  // namespace sim
