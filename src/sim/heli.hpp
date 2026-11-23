#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "world/terrain.hpp"

// Mi-24V flight model.
// Body axes: x = right, y = up, z = back (nose points to -z).
// Body rates (omega): x > 0 nose up, y > 0 nose left, z > 0 left wing down.
namespace sim {

enum class FlightModel { Novice, Stable, Realistic };
const char* flightModelName(FlightModel m);
bool parseFlightModel(const std::string& s, FlightModel& out);

// The helicopter's systems that battle damage can knock out, as the cockpit's failure warning
// lights show them (the manual, item 18): the navigation computer (NAV: no steering to the
// waypoints), the radio (COM: nobody heard, nor the Hip's calls), the infra-red jammer (JAM), the
// threat warning (TWI: the RWR and Natasha dark), the head-up display (HUD), the low light TV and
// the infra-red sensor (LLTV: the gunner's sight magnifies no more; FLIR: no night vision), the
// gun (GUN), the helmet designator (HMTD: the turret does not follow the gunner's look, no T),
// the laser (LAS: no ranging, no designating), the undercarriage (U/C: stays as it is).
enum class System { Nav, Com, Jam, Twi, Hud, Lltv, Flir, Gun, Hmtd, Las, Gear, Count };
constexpr int kSystems = int(System::Count);
const char* systemFailure(System s);  // "NAV FAILURE"

struct Controls {
  float cyclicX = 0.0f;     // -1 left .. +1 right
  float cyclicY = 0.0f;     // -1 back .. +1 forward
  float pedal = 0.0f;       // -1 left .. +1 right
  float collective = 0.0f;  // 0 .. 1.3, torque demand (1.0 = 100 %)
};

class Helicopter {
public:
  static constexpr float kMaxCollective = 1.3f;
  static constexpr float kHoverCollective = 0.78f;  // OGE, sea level, reference mass
  static constexpr float kRefMass = 11000.0f;
  static constexpr float kGravity = 9.81f;
  static constexpr float kRestHeight = 1.78f;  // CG above ground when parked

  explicit Helicopter(const world::Terrain& terrain);

  void resetOnGround(glm::vec2 xz, float headingRad);
  void resetAirborne(glm::vec3 position, float headingRad, float speed);
  void step(const Controls& in, float dt);
  void toggleGear();
  // Disengages the engines from the rotor (they drop to idle) or engages them again: the
  // autorotation drill. Not in Novice, where the lever commands the climb.
  void toggleEngines();

  // Battle damage, 100 = intact. The airframe holds out against rifle fire (the cabins and the
  // engines are armoured), heavy machine guns wear it down; an engine at 0 has failed (the other
  // gives about 60 % torque), a failed tail rotor leaves the pedals dead and the torque turning
  // the helicopter, a holed tank leaks fuel; the airframe at 0 is the end.
  struct Damage {
    float airframe = 100.0f;
    float engine[2] = {100.0f, 100.0f};  // left (No. 1), right
    float tailRotor = 100.0f;
    float fuelLeak = 0.0f;  // kg/s
    bool shotDown = false;
    std::string lastHitBy;  // what hit it last (for the debriefing)
    bool failed[kSystems] = {};  // systems knocked out
  };
  Damage damage;
  enum class HitPart { Airframe, Engine1, Engine2, TailRotor, FuelTank };
  // A hit taking `points` off the part (and something off the airframe); says what happened.
  void hit(HitPart part, float points);
  // A hit knocking a system out (told on the text display).
  void fail(System s);
  bool works(System s) const { return !damage.failed[int(s)]; }
  bool engineOut(int i) const { return damage.engine[i] <= 0.0f; }
  bool engineFire(int i) const { return damage.engine[i] < 30.0f; }
  void setMessage(const std::string& text, float seconds = 3.0f);
  // Hit something in the air: a crash, as on the ground.
  void collide(const char* why) { if (!crashed) crash(why); }
  std::string crashCause;  // what brought it down ("CRASHED", "ROLLED OVER", "MID-AIR COLLISION"...)
  // Points of the airframe and the rotor's tips (body axes) to find collisions in the air by.
  static const std::vector<glm::vec3>& outline();

  // ---- model functions, public so the tests can probe them
  static float rotorEfficiency(float inplaneSpeed);
  static float thrustFactor(float collective, float inplaneSpeed, float axialSpeed, float hubAgl,
                            float densityRatio);
  static glm::vec3 aeroForceBody(const glm::vec3& vBody, float densityRatio);
  // Collective needed for steady level flight at the given speed (sea level, out of ground effect).
  // Also returns the nose-down pitch angle (rad) in *pitchOut if non-null.
  static float levelFlightCollective(float speed, float mass, float* pitchOut = nullptr);

  // ---- derived values
  glm::vec3 forward() const { return orient * glm::vec3(0, 0, -1); }
  glm::vec3 up() const { return orient * glm::vec3(0, 1, 0); }
  glm::vec3 right() const { return orient * glm::vec3(1, 0, 0); }
  float headingDeg() const;  // 0 = north (-z), 90 = east (+x)
  float pitchDeg() const;    // + nose up
  float rollDeg() const;     // + right wing down
  float radarAltitude() const;
  float baroAltitude() const { return pos.y - kRestHeight; }
  float trueAirspeed() const { return glm::length(vel); }

  // ---- state
  glm::vec3 pos{0.0f}, vel{0.0f};
  glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 omega{0.0f};

  FlightModel model = FlightModel::Stable;
  bool noCrash = false;
  float mass = kRefMass;
  bool gearDown = true;
  float gearPos = 1.0f;  // 1 = down and locked, 0 = up
  bool brakes = true;
  bool crashed = false;
  glm::vec3 crashVel{0.0f};  // how it was moving when it hit
  float collective = 0.0f;  // effective collective (= torque / 100)
  float torque = 0.0f;      // displayed torque %
  // Fuel (kg) in the internal tanks; when it runs out the engines stop and the lever does nothing.
  static constexpr float kFuelFull = 2130.0f;
  float fuel = kFuelFull;
  bool infiniteFuel = false;
  float engineRpm = 0.0f, rotorRpm = 0.0f;  // %, both engines alike; rotor droops at high torque
  bool enginesIdle = false;                 // disengaged by the pilot
  bool rotorFree = false;                   // nothing drives the rotor (disengaged, failed, out of fuel)
  float rotorAngle = 0.0f, tailRotorAngle = 0.0f;
  float weightOnWheels = 0.0f;  // 0..1
  std::string message;
  float messageTime = 0.0f;

private:
  void crash(const char* why);
  float groundAt(const glm::vec3& p) const { return terrain_.groundHeight(p.x, p.z); }

  const world::Terrain& terrain_;
  float novSpeedTarget_ = 0.0f;
  // The rotor when nothing drives it, as a fraction of its governed speed. Its stored energy at
  // full speed J (a good deal more than the real rotor's, to leave a player seconds, not two),
  // the shaft power W at 100 % torque, what the air through the disc gives per m/s (N: about the
  // thrust it carries), the speed where it stalls and the most it runs to.
  static constexpr float kRotorEnergy = 22e6f, kRotorPower = 2.6e6f, kRotorDrive = 97000.0f, kRotorStall = 0.4f, kRotorMax = 1.25f;
  static constexpr float kEngineSpool = 4.0f;  // s, from idle to full power
  float rotorSpeed_ = 1.0f;
  float spool_ = 1.0f;
  float novHoldAgl_ = 0.0f;
  float novSettle_ = 0.0f;
  float time_ = 0.0f;
  bool wasOnGround_ = true;
  bool levelling_ = false;  // Stable: off the wheels hands off, the pitch they left it at goes level
};

}  // namespace sim
